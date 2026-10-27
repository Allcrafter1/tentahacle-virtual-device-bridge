// SPDX-License-Identifier: MIT OR Apache-2.0
/* MQTT transport and durable desired-state registry. AVM runs separately. */
#define _GNU_SOURCE
#include <mosquitto.h>
#include <cjson/cJSON.h>
#include "device_registry.h"
#include "registry_store.h"
#include "control_parse.h"
#ifndef TVB_VERSION
#define TVB_VERSION "development"
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>

#define TEXT_SIZE 256
#define MAX_JSON_INTEGER 9007199254740991ULL
struct configuration {
    char host[TEXT_SIZE],username[TEXT_SIZE],password[TEXT_SIZE];
    char client_id[TEXT_SIZE],topic_prefix[TEXT_SIZE],control_socket[TEXT_SIZE];
    char registry_file[TEXT_SIZE],bridge_id[TEXT_SIZE],persist_command[TEXT_SIZE];
    char aha_control[TEXT_SIZE];
    int port;
};
static struct configuration config={
    .client_id="tentahacle",.topic_prefix="tentahacle/tentahacle",
    .control_socket="/var/tmp/aha-virtual-provider.ctl",
    .registry_file="/tmp/flash/tentahacle/registry.json",
    .aha_control="/usr/bin/tentahacle-aha-control",
    .persist_command="/usr/bin/modsave flash",.port=1883
};
static struct mosquitto *mqtt;
static volatile sig_atomic_t running=1;
static pthread_mutex_t registry_lock=PTHREAD_MUTEX_INITIALIZER;
static tvb_device_registry registry;
/* Protected by registry_lock, including Provider requests and delayed UNITs. */
static int provider_ready;
static int flash_dirty;
static int debug_enabled;
static double provider_generation=-1;
static time_t unit_due;
/* AHA restarts can leave a stored 440 widget absent from its physical display.
 * Retry native readiness per target, never re-submit an already completed one. */
/* Native widget bindings may take about a minute even for an already known
 * unit. Leave at least 90 seconds of retries with just one enabled target. */
#define SCREEN_RESYNC_ATTEMPTS 45
static time_t screen_resync_due;
static unsigned char screen_resync_attempts[TVB_REGISTRY_CAPACITY];
static size_t screen_resync_cursor;
static int provider_request(const char *request);
enum feedback_kind {
    FEEDBACK_SET, FEEDBACK_LEVEL, FEEDBACK_COLOR_TEMP, FEEDBACK_HS,
    FEEDBACK_POSITION, FEEDBACK_TARGET, FEEDBACK_ACTUAL, FEEDBACK_MODE,
    FEEDBACK_TIMER, FEEDBACK_SCHEDULE, FEEDBACK_COUNT
};
static char feedback_cache[TVB_REGISTRY_CAPACITY][FEEDBACK_COUNT][128];
static const char *const feedback_operations[]={
    "SET","LEVEL","COLOR_TEMP","HS","POSITION","TARGET","ACTUAL","MODE","TIMER","SCHEDULE"
};
static void bridge_debug(const char *format,...){
    if(!debug_enabled)return;
    struct timespec now={0};long long milliseconds=-1;
    if(!clock_gettime(CLOCK_MONOTONIC,&now))
        milliseconds=(long long)now.tv_sec*1000+now.tv_nsec/1000000;
    flockfile(stderr);
    fprintf(stderr,"debug monotonic_ms=%lld mqtt_bridge: ",milliseconds);
    va_list arguments;va_start(arguments,format);vfprintf(stderr,format,arguments);va_end(arguments);
    funlockfile(stderr);
}
static void debug_feedback(const char *operation,const char *uid,const char *request,const char *status){
    const char *value=strchr(request,' ');if(value)value=strchr(value+1,' ');
    value=value?value+1:"";size_t length=strlen(value);
    while(length&&(value[length-1]=='\n'||value[length-1]=='\r'))length--;
    bridge_debug("feedback uid=%s property=%s value=%.*s provider=%s\n",
        uid,operation,(int)length,value,status);
}
static int debug_requested(const char *value){
    return value&&(!strcmp(value,"1")||!strcasecmp(value,"yes")||
        !strcasecmp(value,"true"));
}
static int feedback_request(const char *request){
    char operation[24],uid[20];
    if(sscanf(request,"%23s %19s",operation,uid)!=2||strlen(request)>=128)return 0;
    const tvb_device *device=tvb_registry_find(&registry,uid);if(!device)return 0;
    for(size_t i=0;i<FEEDBACK_COUNT;i++)if(!strcmp(operation,feedback_operations[i])){
        size_t row=(size_t)(device-registry.devices);
        if(i==FEEDBACK_COLOR_TEMP){
            feedback_cache[row][FEEDBACK_HS][0]=0;
            bridge_debug("feedback uid=%s property=HS value=(cleared) provider=cache_only\n",uid);
        }else if(i==FEEDBACK_HS){
            feedback_cache[row][FEEDBACK_COLOR_TEMP][0]=0;
            bridge_debug("feedback uid=%s property=COLOR_TEMP value=(cleared) provider=cache_only\n",uid);
        }
        strcpy(feedback_cache[row][i],request);
        if(!provider_ready){debug_feedback(operation,uid,request,"deferred");return 1;}
        if(provider_request(request)){debug_feedback(operation,uid,request,"accepted");return 1;}
        debug_feedback(operation,uid,request,"rejected");
        provider_ready=0;unit_due=0;return 0;
    }
    return 0;
}
static char topic_availability[TEXT_SIZE*2],topic_info[TEXT_SIZE*2];
static char topic_registry[TEXT_SIZE*2],topic_management[TEXT_SIZE*2];
static void stop_handler(int signum){(void)signum;running=0;}
static char *trim(char *text){
    while(*text==' '||*text=='\t')text++;
    char *end=text+strlen(text);
    while(end>text&&(end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'||end[-1]=='\n'))*--end=0;
    return text;
}
static int copy_value(char *target,size_t size,const char *value){
    if(strlen(value)>=size)return 0;
    strcpy(target,value);return 1;
}
static int identifier_valid(const char *id,size_t maximum){
    if(!id||strlen(id)<3||strlen(id)>maximum)return 0;
    for(const unsigned char *p=(const unsigned char*)id;*p;p++)
        if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return 0;
    return 1;
}
static int prefix_valid(const char *prefix){
    if(!prefix||!*prefix||*prefix=='/'||prefix[strlen(prefix)-1]=='/'||strstr(prefix,"//"))return 0;
    for(const unsigned char *p=(const unsigned char*)prefix;*p;p++)if(*p<=32||*p==127||*p=='+'||*p=='#')return 0;
    return 1;
}
static int read_config(const char *path){
    FILE *file=fopen(path,"r");
    if(!file){fprintf(stderr,"mqtt_bridge: cannot open configuration: %s\n",strerror(errno));return 0;}
    char line[1024];int valid=1;
    while(fgets(line,sizeof(line),file)){
        if(!strchr(line,'\n')&&!feof(file)){valid=0;break;}
        char *key=trim(line);if(!*key||*key=='#')continue;
        char *value=strchr(key,'=');if(!value){valid=0;break;}
        *value++=0;key=trim(key);value=trim(value);
#define SETTING(field) if(!strcmp(key,#field))valid&=copy_value(config.field,sizeof(config.field),value)
        SETTING(host);else SETTING(username);else SETTING(password);
        else SETTING(client_id);else SETTING(topic_prefix);else SETTING(control_socket);
        else SETTING(registry_file);else SETTING(bridge_id);else SETTING(persist_command);
        else SETTING(aha_control);
        else if(!strcmp(key,"port")){char *end;long n=strtol(value,&end,10);valid&=*value&&!*end&&n>0&&n<65536;config.port=(int)n;}
        else {fprintf(stderr,"mqtt_bridge: unknown setting %s\n",key);valid=0;}
#undef SETTING
    }
    if(ferror(file))valid=0;
    fclose(file);
    if(!*config.bridge_id)copy_value(config.bridge_id,sizeof(config.bridge_id),config.client_id);
    if(!*config.host||!*config.client_id||!*config.topic_prefix||!*config.control_socket||!*config.registry_file||
       !*config.aha_control||config.aha_control[0]!='/'||
       !prefix_valid(config.topic_prefix)||!identifier_valid(config.bridge_id,32))valid=0;
    return valid;
}
static int provider_connect(void){
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return -1;
    struct sockaddr_un address={.sun_family=AF_UNIX};
    if(strlen(config.control_socket)>=sizeof(address.sun_path)){close(fd);return -1;}
    strcpy(address.sun_path,config.control_socket);
    struct timeval timeout={2,0};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) ||
       setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))){close(fd);return -1;}
    if(connect(fd,(struct sockaddr*)&address,sizeof(address))){close(fd);return -1;}
    return fd;
}
static cJSON *provider_query(const char *request){
    int fd=provider_connect();if(fd<0)return NULL;
    char reply[4096];size_t size=strlen(request);ssize_t n=-1;
    ssize_t sent;
    do {sent=send(fd,request,size,MSG_NOSIGNAL);}while(sent<0&&errno==EINTR);
    if(sent==(ssize_t)size){
        do {n=recv(fd,reply,sizeof(reply)-1,MSG_TRUNC);}while(n<0&&errno==EINTR);
    }
    close(fd);
    if(n<=0||n>=(ssize_t)sizeof(reply))return NULL;
    if(memchr(reply,0,(size_t)n))return NULL;
    reply[n]=0;return cJSON_ParseWithOpts(reply,NULL,1);
}
static int provider_request(const char *request){
    cJSON *reply=provider_query(request);
    int ok=reply&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"ok"));
    cJSON_Delete(reply);return ok;
}
/* Both external helpers are bounded. In particular a stuck flash-save helper
 * must not hold registry_lock forever and prevent MQTT/provider recovery. */
static int wait_child(pid_t child, unsigned seconds, int abort_on_shutdown){
    struct timespec start,now;
    if(clock_gettime(CLOCK_MONOTONIC,&start))goto terminate;
    for(;;){
        int status;pid_t waited=waitpid(child,&status,WNOHANG);
        if(waited==child)return WIFEXITED(status)&&WEXITSTATUS(status)==0;
        if(waited<0&&errno!=EINTR)return 0;
        if(abort_on_shutdown&&!running)break;
        if(clock_gettime(CLOCK_MONOTONIC,&now))break;
        if(now.tv_sec-start.tv_sec>(time_t)seconds ||
           (now.tv_sec-start.tv_sec==(time_t)seconds&&now.tv_nsec>=start.tv_nsec))break;
        struct timespec pause={0,100000000};
        (void)nanosleep(&pause,NULL);
    }
terminate:
    kill(child,SIGKILL);
    while(waitpid(child,NULL,0)<0&&errno==EINTR){}
    return 0;
}
static int native_delete(const char *uid){
    if(!running)return 0;
    pid_t child=fork();
    if(child<0)return 0;
    if(!child){
        /* The bridge uses Freetz libraries, but this helper loads AVM's
         * libaha and must use the firmware's matching loader/library stack. */
        if(unsetenv("LD_LIBRARY_PATH"))_exit(126);
        char *const arguments[]={config.aha_control,"delete",(char*)uid,NULL};
        execv(config.aha_control,arguments);_exit(127);
    }
    return wait_child(child,4,1);
}
static int native_resync(const tvb_device *device){
    if(!running)return 0;
    pid_t child=fork();
    if(child<0)return 0;
    if(!child){
        if(unsetenv("LD_LIBRARY_PATH"))_exit(126);
        char *const arguments[]={config.aha_control,"resync",(char*)device->uid,
            (char*)tvb_profile_name(device->profile),NULL};
        execv(config.aha_control,arguments);_exit(127);
    }
    return wait_child(child,4,1);
}
static void resync_screens_locked(void){
    if(!provider_ready||!screen_resync_due||time(NULL)<screen_resync_due)return;
    for(size_t offset=0;offset<registry.count;offset++){
        size_t i=(screen_resync_cursor+offset)%registry.count;
        if(!registry.devices[i].enabled||!screen_resync_attempts[i])continue;
        if(native_resync(&registry.devices[i]))screen_resync_attempts[i]=0;
        else if(!--screen_resync_attempts[i])
            fprintf(stderr,"mqtt_bridge: native screen resync failed for %s after bounded retries\n",registry.devices[i].uid);
        screen_resync_cursor=(i+1)%registry.count;
        /* At most one bounded native call while holding registry_lock. */
        break;
    }
    screen_resync_due=0;
    for(size_t i=0;i<registry.count;i++)if(registry.devices[i].enabled&&screen_resync_attempts[i]){
        screen_resync_due=time(NULL)+2;break;
    }
}
static int is_hanfun(tvb_device_profile profile){
    return profile==TVB_PROFILE_DIMMABLE_LIGHT||profile==TVB_PROFILE_COLOR_TEMPERATURE_LIGHT||profile==TVB_PROFILE_COVER;
}
static int provider_device(const tvb_device *device){
    char command[256];
    snprintf(command,sizeof(command),"RESTORE %s %u %s %s",device->uid,
        device->remote_id,tvb_profile_name(device->profile),device->name);
    cJSON *reply=provider_query(command);
    cJSON *id=cJSON_GetObjectItemCaseSensitive(reply,"remote_id");
    const cJSON *profile=cJSON_GetObjectItemCaseSensitive(reply,"profile");
    int identity_ok=reply&&cJSON_IsNumber(id)&&id->valuedouble==device->remote_id&&
        cJSON_IsString(profile)&&!strcmp(profile->valuestring,tvb_profile_name(device->profile));
    int ok=identity_ok&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"ok"));
    if(!identity_ok)fprintf(stderr,"mqtt_bridge: provider identity mismatch for %s (expected remote_id %u)\n",device->uid,device->remote_id);
    cJSON_Delete(reply);
    if(!identity_ok)return 0;
    if(!ok){
        snprintf(command,sizeof(command),"RENAME %s %s",device->uid,device->name);
        if(!provider_request(command))return 0;
    }
    if(device->profile==TVB_PROFILE_COLOR_TEMPERATURE_LIGHT){
        snprintf(command,sizeof(command),"COLOR_CAPABILITY %s %s",device->uid,
            device->color_full?"full":"temperature");
        if(!provider_request(command))return 0;
    }
    snprintf(command,sizeof(command),"%s %s",device->enabled?"ENABLE":"DISABLE",device->uid);
    if(!provider_request(command))return 0;
    if(device->enabled){
        snprintf(command,sizeof(command),"ANNOUNCE %s",device->uid);
        if(!provider_request(command))return 0;
    }
    return 1;
}
/* Restore the durable IDs explicitly; deleted devices leave allocator gaps. */
static int reconcile_provider_locked(void){
    provider_ready=0;unit_due=0;screen_resync_due=0;
    memset(screen_resync_attempts,0,sizeof(screen_resync_attempts));
    screen_resync_cursor=0;
    cJSON *status=provider_query("GET");
    cJSON *generation=cJSON_GetObjectItemCaseSensitive(status,"generation");
    int connected=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(status,"connected"))&&cJSON_IsNumber(generation);
    if(connected)provider_generation=generation->valuedouble;
    cJSON_Delete(status);if(!connected)return 0;
    for(size_t i=0;i<registry.count;i++)if(!provider_device(&registry.devices[i]))return 0;
    unit_due=time(NULL)+3;return 1;
}
static int finish_units_locked(void){
    if(!unit_due||time(NULL)<unit_due)return 0;
    for(size_t i=0;i<registry.count;i++){
        const tvb_device *d=&registry.devices[i];
        if(d->enabled&&is_hanfun(d->profile)){
            char command[64];snprintf(command,sizeof(command),"UNIT %s",d->uid);
            if(!provider_request(command)){unit_due=0;return 0;}
        }
    }
    unit_due=0;provider_ready=1;
    /* SCHEDULE initializes the provider's target. Restore the authoritative
     * target/mode afterwards, otherwise an active schedule turns an OFF
     * thermostat back to HEAT on each provider reconnect. */
    static const unsigned replay_order[]={
        FEEDBACK_SCHEDULE,FEEDBACK_SET,FEEDBACK_LEVEL,FEEDBACK_COLOR_TEMP,
        FEEDBACK_HS,FEEDBACK_POSITION,FEEDBACK_ACTUAL,FEEDBACK_TARGET,
        FEEDBACK_MODE,FEEDBACK_TIMER
    };
    for(size_t i=0;i<registry.count;i++)if(registry.devices[i].enabled)
        for(size_t order=0;order<FEEDBACK_COUNT;order++){
            size_t kind=replay_order[order];
            if(!*feedback_cache[i][kind])continue;
            char *command=feedback_cache[i][kind];
            if(kind==FEEDBACK_TIMER){
                unsigned long end;char uid[20],mode[8];
                if(sscanf(command,"TIMER %19s %7s %lu",uid,mode,&end)==3&&end<=(unsigned long)time(NULL))
                    snprintf(command,128,"TIMER %s cancel",registry.devices[i].uid);
            }
            if(!provider_request(command)){provider_ready=0;return 0;}
        }
    for(size_t i=0;i<registry.count;i++)if(registry.devices[i].enabled)screen_resync_attempts[i]=SCREEN_RESYNC_ATTEMPTS;
    screen_resync_due=time(NULL)+1;
    return 1;
}
static cJSON *registry_json_locked(void){
    char *json=tvb_registry_serialize(&registry);
    if(!json)return NULL;
    cJSON *root=cJSON_Parse(json);tvb_registry_serialized_free(json);
    if(root&&!cJSON_AddStringToObject(root,"bridge_id",config.bridge_id)){cJSON_Delete(root);return NULL;}
    return root;
}
static void publish_json(const char *topic,const cJSON *json,int retained){
    char *text=cJSON_PrintUnformatted(json);if(!text)return;
    mosquitto_publish(mqtt,NULL,topic,(int)strlen(text),text,1,retained!=0);
    cJSON_free(text);
}
static void publish_registry_locked(void){
    cJSON *root=registry_json_locked();if(root){publish_json(topic_registry,root,1);cJSON_Delete(root);}
    root=cJSON_CreateObject();if(!root)return;
    if(!cJSON_AddNumberToObject(root,"schema_version",1)||
       !cJSON_AddStringToObject(root,"bridge_id",config.bridge_id)||
       !cJSON_AddStringToObject(root,"bridge_version",TVB_VERSION)||
       !cJSON_AddBoolToObject(root,"ready",provider_ready)||
       !cJSON_AddNumberToObject(root,"revision",(double)registry.revision)){cJSON_Delete(root);return;}
    cJSON *profiles=cJSON_AddArrayToObject(root,"profiles");
    if(!profiles){cJSON_Delete(root);return;}
    for(unsigned i=0;i<TVB_PROFILE_COUNT;i++){
        cJSON *profile=cJSON_CreateString(tvb_profile_name((tvb_device_profile)i));
        if(!profile||!cJSON_AddItemToArray(profiles,profile)){cJSON_Delete(profile);cJSON_Delete(root);return;}
    }
    publish_json(topic_info,root,1);cJSON_Delete(root);
}
/* No shell expansion: config contains an executable and whitespace-separated
 * arguments. Empty disables flash persistence (atomic runtime file remains). */
static int persist_flash(void){
    if(!*config.persist_command)return 1;
    char text[TEXT_SIZE],*args[17],*save=NULL;size_t count=0;
    strcpy(text,config.persist_command);
    for(char *p=strtok_r(text," \t",&save);p;p=strtok_r(NULL," \t",&save)){
        if(count==16)return 0;
        args[count++]=p;
    }
    if(!count)return 1;
    args[count]=NULL;
    if(args[0][0]!='/')return 0;
    pid_t child=fork();if(child<0)return 0;
    if(!child){execv(args[0],args);_exit(127);}
    /* Never kill modsave on shutdown: interrupting a flash update is riskier
     * than a bounded slow service stop.  The init script waits past this cap. */
    return wait_child(child,30,0);
}
static int json_integer(const cJSON *v,uint64_t *result){
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>MAX_JSON_INTEGER)return 0;
    uint64_t n=(uint64_t)v->valuedouble;if((double)n!=v->valuedouble)return 0;
    *result=n;return 1;
}
static const char *json_string(const cJSON *root,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,key);
    return cJSON_IsString(v)?v->valuestring:NULL;
}
static int request_id_valid(const char *id){return identifier_valid(id,64);}
static int keys_valid(const cJSON *object,const char *const *keys){
    if(!cJSON_IsObject(object))return 0;
    for(const cJSON *item=object->child;item;item=item->next){
        size_t i=0;while(keys[i]&&strcmp(keys[i],item->string))i++;
        if(!keys[i])return 0;
        for(const cJSON *other=item->next;other;other=other->next)if(!strcmp(item->string,other->string))return 0;
    }
    return 1;
}
static int parse_profile(const char *name,tvb_device_profile *profile){
    if(!name)return 0;
    for(unsigned i=0;i<TVB_PROFILE_COUNT;i++)if(!strcmp(name,tvb_profile_name((tvb_device_profile)i))){*profile=(tvb_device_profile)i;return 1;}
    return 0;
}
/* Pure candidate builder, also exercised by the host test. */
static const char *management_candidate(const cJSON *root,tvb_device_registry *candidate,int *mutate,int *reannounce){
    static const char *const keys[]={"schema_version","request_id","operation","expected_revision","device","uid","name","enabled","enabled_uids","keep_uids",NULL};
    static const char *const device_keys[]={"uid","profile","name","enabled","color_full",NULL};
    uint64_t version,expected;*mutate=0;*reannounce=0;
    if(!keys_valid(root,keys)||!json_integer(cJSON_GetObjectItemCaseSensitive(root,"schema_version"),&version)||version!=1)return "invalid_schema";
    const char *operation=json_string(root,"operation");if(!operation)return "invalid_operation";
    const cJSON *revision=cJSON_GetObjectItemCaseSensitive(root,"expected_revision");
    if(revision&&(!json_integer(revision,&expected)||expected!=candidate->revision))return "revision_conflict";
    if(!strcmp(operation,"list_devices"))return NULL;
    if(!strcmp(operation,"reannounce")){*reannounce=1;return NULL;}
    tvb_registry_result result=TVB_REGISTRY_INVALID;
    if(!strcmp(operation,"upsert_device")){
        const cJSON *device=cJSON_GetObjectItemCaseSensitive(root,"device");
        if(!keys_valid(device,device_keys))return "invalid_device";
        const char *uid=json_string(device,"uid"),*name=json_string(device,"name");
        tvb_device_profile profile;
        const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(device,"enabled");
        const cJSON *color_full=cJSON_GetObjectItemCaseSensitive(device,"color_full");
        if(!tvb_registry_valid_uid(uid)||!tvb_registry_valid_name(name)||name[0]==' '||!parse_profile(json_string(device,"profile"),&profile)||
           (enabled&&!cJSON_IsBool(enabled))||(color_full&&!cJSON_IsBool(color_full))||
           (color_full&&profile!=TVB_PROFILE_COLOR_TEMPERATURE_LIGHT))return "invalid_device";
        const tvb_device *existing=tvb_registry_find(candidate,uid);
        if(existing&&existing->profile!=profile)return "immutable_profile";
        if(existing)result=tvb_registry_rename(candidate,candidate->revision,uid,name);
        else result=tvb_registry_add(candidate,candidate->revision,uid,name,profile,NULL);
        if(result==TVB_REGISTRY_OK&&color_full)result=tvb_registry_set_color_full(candidate,candidate->revision,uid,cJSON_IsTrue(color_full));
        if(result==TVB_REGISTRY_OK&&enabled)result=tvb_registry_set_enabled(candidate,candidate->revision,uid,cJSON_IsTrue(enabled));
    }else if(!strcmp(operation,"rename_device")){
        const char *name=json_string(root,"name");
        if(!tvb_registry_valid_name(name)||name[0]==' ')return "invalid_name";
        result=tvb_registry_rename(candidate,candidate->revision,json_string(root,"uid"),json_string(root,"name"));
    }else if(!strcmp(operation,"set_enabled")){
        const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(root,"enabled");
        if(!cJSON_IsBool(enabled))return "invalid_enabled";
        result=tvb_registry_set_enabled(candidate,candidate->revision,json_string(root,"uid"),cJSON_IsTrue(enabled));
    }else if(!strcmp(operation,"reconcile_devices")){
        const cJSON *uids=cJSON_GetObjectItemCaseSensitive(root,"enabled_uids");
        if(!cJSON_IsArray(uids)||cJSON_GetArraySize(uids)>(int)TVB_REGISTRY_CAPACITY)return "invalid_enabled_uids";
        for(const cJSON *uid=uids->child;uid;uid=uid->next){
            if(!cJSON_IsString(uid)||!tvb_registry_valid_uid(uid->valuestring)||!tvb_registry_find(candidate,uid->valuestring))return "unknown_uid";
            const tvb_device *known=tvb_registry_find(candidate,uid->valuestring);
            for(const cJSON *other=uid->next;other;other=other->next)
                if(cJSON_IsString(other)&&tvb_registry_find(candidate,other->valuestring)==known)return "duplicate_uid";
        }
        for(size_t i=0;i<candidate->count;i++){
            int enabled=0;
            for(const cJSON *uid=uids->child;uid;uid=uid->next)
                if(tvb_registry_find(candidate,uid->valuestring)==&candidate->devices[i])enabled=1;
            result=tvb_registry_set_enabled(candidate,candidate->revision,candidate->devices[i].uid,enabled);
            if(result!=TVB_REGISTRY_OK)return "registry_error";
        }
        result=TVB_REGISTRY_OK;
    }else if(!strcmp(operation,"prune_devices")){
        const cJSON *uids=cJSON_GetObjectItemCaseSensitive(root,"keep_uids");
        if(!cJSON_IsArray(uids)||cJSON_GetArraySize(uids)>(int)TVB_REGISTRY_CAPACITY)return "invalid_keep_uids";
        for(const cJSON *uid=uids->child;uid;uid=uid->next){
            if(!cJSON_IsString(uid)||!tvb_registry_valid_uid(uid->valuestring))return "invalid_keep_uids";
            for(const cJSON *other=uid->next;other;other=other->next)
                if(cJSON_IsString(other)&&!strcasecmp(uid->valuestring,other->valuestring))return "duplicate_uid";
        }
        for(size_t i=candidate->count;i>0;i--){
            const char *candidate_uid=candidate->devices[i-1].uid;int keep=0;
            for(const cJSON *uid=uids->child;uid;uid=uid->next)
                if(!strcasecmp(uid->valuestring,candidate_uid))keep=1;
            if(!keep){
                result=tvb_registry_remove(candidate,candidate->revision,candidate_uid);
                if(result!=TVB_REGISTRY_OK)return "registry_error";
            }
        }
        result=TVB_REGISTRY_OK;
    }else return "unknown_operation";
    if(result!=TVB_REGISTRY_OK)return result==TVB_REGISTRY_FULL?"registry_full":result==TVB_REGISTRY_NOT_FOUND?"unknown_uid":"invalid_device";
    *mutate=1;return NULL;
}
/* Registry mutations append or delete without reordering surviving devices.
 * Thus old_index >= i and a forward compaction cannot overwrite an unread
 * cache row. New UIDs always receive an empty row, never a removed device's. */
static void remap_feedback_cache(const tvb_device_registry *old_registry){
    for(size_t i=0;i<registry.count;i++){
        const tvb_device *old=tvb_registry_find(old_registry,registry.devices[i].uid);
        if(old){
            size_t old_index=(size_t)(old-old_registry->devices);
            if(old_index!=i)memmove(feedback_cache[i],feedback_cache[old_index],sizeof(feedback_cache[i]));
        }else memset(feedback_cache[i],0,sizeof(feedback_cache[i]));
    }
    for(size_t i=registry.count;i<TVB_REGISTRY_CAPACITY;i++)memset(feedback_cache[i],0,sizeof(feedback_cache[i]));
    for(size_t i=0;i<registry.count;i++)if(!registry.devices[i].color_full)
        feedback_cache[i][FEEDBACK_HS][0]=0;
}
static void management_locked(const struct mosquitto_message *message){
    if(message->retain||message->payloadlen<=0||message->payloadlen>8192||memchr(message->payload,0,(size_t)message->payloadlen))return;
    char payload[8193];memcpy(payload,message->payload,(size_t)message->payloadlen);payload[message->payloadlen]=0;
    /* cJSON exposes C strings; reject embedded decoded NULs rather than
     * silently interpreting a truncated UID/name/operation. */
    if(tvb_json_has_decoded_nul(payload))return;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithOpts(payload,&end,1);if(!root)return;
    const char *id=json_string(root,"request_id");if(!request_id_valid(id)){cJSON_Delete(root);return;}
    tvb_device_registry candidate=registry;int mutate,reannounce;
    const char *error=management_candidate(root,&candidate,&mutate,&reannounce);
    int accepted=0,flash_saved=1;
    char storage_error[256];
    if(!error){
        if(mutate&&candidate.revision!=registry.revision){
            char removed[TVB_REGISTRY_CAPACITY][TVB_DEVICE_UID_BYTES];size_t removed_count=0;
            for(size_t i=0;i<registry.count;i++)if(!tvb_registry_find(&candidate,registry.devices[i].uid))
                strcpy(removed[removed_count++],registry.devices[i].uid);
            /* Native deletion/REMOVE may succeed before a later helper or
             * registry write fails. A healthy generation cannot detect that
             * divergence. Mark recovery pending before the first side effect;
             * on failure the watch loop replays the unchanged desired registry. */
            if(removed_count){
                provider_ready=0;unit_due=0;screen_resync_due=0;
            }
            for(size_t i=0;i<removed_count&&!error;i++)if(!native_delete(removed[i]))error="native_delete_failed";
            for(size_t i=0;i<removed_count&&!error;i++){
                char command[64];snprintf(command,sizeof(command),"REMOVE %.19s",removed[i]);
                if(!provider_request(command))error="provider_remove_failed";
            }
            if(!error){
                if(!tvb_registry_save_file(config.registry_file,&candidate,storage_error,sizeof(storage_error)))error="registry_save_failed";
                else {
                    tvb_device_registry old_registry=registry;
                    flash_saved=persist_flash();flash_dirty=!flash_saved;registry=candidate;accepted=1;
                    remap_feedback_cache(&old_registry);
                    if(!reconcile_provider_locked())error="provider_pending";
                    if(!flash_saved)error="flash_persist_failed";
                }
            }
        }else {
            accepted=1;
            if(reannounce||(mutate&&!provider_ready&&!unit_due))if(!reconcile_provider_locked())error="provider_pending";
            if(mutate&&flash_dirty){flash_saved=persist_flash();flash_dirty=!flash_saved;if(!flash_saved)error="flash_persist_failed";}
        }
    }
    publish_registry_locked();
    cJSON *response=cJSON_CreateObject();
    if(response){
        int complete=cJSON_AddNumberToObject(response,"schema_version",1)&&
            cJSON_AddStringToObject(response,"bridge_id",config.bridge_id)&&
            cJSON_AddStringToObject(response,"request_id",id)&&cJSON_AddBoolToObject(response,"ok",accepted&&!error)&&
            cJSON_AddBoolToObject(response,"accepted",accepted)&&cJSON_AddBoolToObject(response,"ready",provider_ready)&&
            cJSON_AddBoolToObject(response,"flash_persisted",!flash_dirty)&&
            cJSON_AddNumberToObject(response,"revision",(double)registry.revision);
        if(complete&&error)complete=cJSON_AddStringToObject(response,"error",error)!=NULL;
        cJSON *snapshot=complete?registry_json_locked():NULL;
        if(!snapshot||!cJSON_AddItemToObject(response,"registry",snapshot)){cJSON_Delete(snapshot);complete=0;}
        char topic[TEXT_SIZE*2];snprintf(topic,sizeof(topic),"%s/management/response/%s",config.topic_prefix,id);
        if(complete)publish_json(topic,response,0);
        cJSON_Delete(response);
    }
    cJSON_Delete(root);
}
static int parse_state(const void *payload,int length,int *state){
    if(length<=0||length>15)return 0;
    char value[16];memcpy(value,payload,(size_t)length);value[length]=0;
    char *v=trim(value);
    if(!strcasecmp(v,"ON")||!strcasecmp(v,"TRUE")||!strcmp(v,"1")){*state=1;return 1;}
    if(!strcasecmp(v,"OFF")||!strcasecmp(v,"FALSE")||!strcmp(v,"0")){*state=0;return 1;}
    return 0;
}

/* AHA 8.25 matches color-temperature feedback against these exact values.
 * Keep this final MQTT boundary defensive as retained data may have been
 * produced by an older integration.  The ascending order plus strict '<'
 * comparison makes an exact midpoint select the warmer (lower-Kelvin) step,
 * matching Home Assistant's protocol normalization. */
static long normalize_color_temperature(long value){
    static const long palette[]={2700,3000,3400,3800,4200,4700,5300,5900,6500};
    long closest=palette[0],distance=labs(value-palette[0]);
    for(size_t i=1;i<sizeof(palette)/sizeof(*palette);i++){
        long candidate_distance=labs(value-palette[i]);
        if(candidate_distance<distance){closest=palette[i];distance=candidate_distance;}
    }
    return closest;
}

static void device_message_locked(struct mosquitto *client,void *userdata,const struct mosquitto_message *message){
    (void)client;(void)userdata;
    size_t prefix_length=strlen(config.topic_prefix);
    const char *device_prefix="/device/";
    const char *suffixes[]={"/state/set","/level/set","/color_temperature/set","/hs/set","/position/set","/target_temperature/set","/current_temperature/set","/hvac_mode/set","/timer/set","/schedule/set"};
    if(strncmp(message->topic,config.topic_prefix,prefix_length)||
       strncmp(message->topic+prefix_length,device_prefix,strlen(device_prefix)))return;
    const char *endpoint=message->topic+prefix_length+strlen(device_prefix);
    const char *suffix=0;unsigned kind;
    for(kind=0;kind<FEEDBACK_COUNT;kind++){
        const char *candidate=strstr(endpoint,suffixes[kind]);
        if(candidate&&!strcmp(candidate,suffixes[kind])){suffix=candidate;break;}
    }
    if(!suffix||kind==FEEDBACK_COUNT||suffix==endpoint||suffix-endpoint>31)return;
    char endpoint_id[32];memcpy(endpoint_id,endpoint,(size_t)(suffix-endpoint));endpoint_id[suffix-endpoint]=0;
    const tvb_device *device=tvb_registry_find(&registry,endpoint_id);
    if(!device||!device->enabled)return;
    tvb_device_profile profile=device->profile;
    if(message->payloadlen==0&&(kind==2||kind==3)){
        feedback_cache[device-registry.devices][kind][0]=0;
        bridge_debug("feedback uid=%s property=%s value=(cleared) provider=cache_only\n",
            endpoint_id,feedback_operations[kind]);
        return;
    }
    if((kind==FEEDBACK_SET&&profile!=TVB_PROFILE_SWITCH&&profile!=TVB_PROFILE_DIMMABLE_LIGHT&&profile!=TVB_PROFILE_COLOR_TEMPERATURE_LIGHT)||
       (kind==FEEDBACK_LEVEL&&profile!=TVB_PROFILE_DIMMABLE_LIGHT&&profile!=TVB_PROFILE_COLOR_TEMPERATURE_LIGHT)||
       (kind==FEEDBACK_COLOR_TEMP&&profile!=TVB_PROFILE_COLOR_TEMPERATURE_LIGHT)||
       (kind==FEEDBACK_HS&&(profile!=TVB_PROFILE_COLOR_TEMPERATURE_LIGHT||!device->color_full))||
       (kind==FEEDBACK_POSITION&&profile!=TVB_PROFILE_COVER)||
       (kind>=FEEDBACK_TARGET&&profile!=TVB_PROFILE_THERMOSTAT))return;
    if(message->payloadlen<=0||message->payloadlen>127)return;
    for(int i=0;i<message->payloadlen;i++)if(((const unsigned char*)message->payload)[i]<32)return;
    if(kind==FEEDBACK_TARGET){
        if(message->payloadlen==3&&!memcmp(message->payload,"max",3)){
            char request[80];snprintf(request,sizeof(request),"TARGET %s max\n",endpoint_id);
            if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected thermostat MAX target\n");
            return;
        }
        if(message->payloadlen<=0||message->payloadlen>5)return;
        char value_text[8];memcpy(value_text,message->payload,(size_t)message->payloadlen);value_text[message->payloadlen]=0;
        char *end=0;double value=strtod(value_text,&end);
        if(!isfinite(value)||value<8.0||value>28.0)return;
        double scaled=value*2.0;long half=(long)(scaled+0.5);
        double difference=scaled-(double)half;if(difference<0)difference=-difference;
        if(end==value_text||*end||value<8.0||value>28.0||difference>0.01){
            fprintf(stderr,"mqtt_bridge: ignored invalid thermostat target\n");return;
        }
        char request[80];snprintf(request,sizeof(request),"TARGET %s %.1f\n",endpoint_id,value);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected thermostat target %.1f\n",value);
        return;
    }
    if(kind==FEEDBACK_ACTUAL){
        if(message->payloadlen==7&&!memcmp(message->payload,"unknown",7)){
            char request[80];snprintf(request,sizeof(request),"ACTUAL %s unknown\n",endpoint_id);
            if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected unknown actual temperature\n");
            return;
        }
        if(message->payloadlen<=0||message->payloadlen>6)return;
        char value_text[8];memcpy(value_text,message->payload,(size_t)message->payloadlen);value_text[message->payloadlen]=0;
        char *end=0;double value=strtod(value_text,&end);
        int in_range=isfinite(value)&&value>= -50.0&&value<=100.0;
        double scaled=in_range?value*10.0:0;
        long deci=in_range?(long)(scaled+(scaled>=0?0.5:-0.5)):0;
        double difference=scaled-(double)deci;if(difference<0)difference=-difference;
        if(end==value_text||*end||!in_range||difference>0.01){
            fprintf(stderr,"mqtt_bridge: ignored invalid actual temperature\n");return;
        }
        unsigned long magnitude=(unsigned long)(deci<0?-deci:deci);
        char request[80];snprintf(request,sizeof(request),"ACTUAL %s %s%lu.%lu\n",endpoint_id,
            deci<0?"-":"",magnitude/10,magnitude%10);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected actual temperature\n");
        return;
    }
    if(kind==FEEDBACK_MODE){
        if(message->payloadlen<=0||message->payloadlen>4)return;
        char mode[5];memcpy(mode,message->payload,(size_t)message->payloadlen);mode[message->payloadlen]=0;
        if(strcmp(mode,"off")&&strcmp(mode,"heat")){fprintf(stderr,"mqtt_bridge: ignored invalid HVAC mode\n");return;}
        char request[80];snprintf(request,sizeof(request),"MODE %s %s\n",endpoint_id,mode);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected HVAC mode %s\n",mode);
        return;
    }
    if(kind==FEEDBACK_TIMER){
        if(message->payloadlen<=0||message->payloadlen>31)return;
        char value[32];memcpy(value,message->payload,(size_t)message->payloadlen);value[message->payloadlen]=0;
        if(strcmp(value,"cancel")){
            char *space=strchr(value,' ');if(!space)return;*space++=0;
            if(strcmp(value,"boost")&&strcmp(value,"cold"))return;
            uint32_t timestamp;
            if(!tvb_parse_timestamp(space,&timestamp))return;
            *--space=' ';
        }
        char request[80];snprintf(request,sizeof(request),"TIMER %s %s\n",endpoint_id,value);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected thermostat timer %s\n",value);
        return;
    }
    if(kind==FEEDBACK_SCHEDULE){
        if(message->payloadlen<=0||message->payloadlen>63)return;
        char value[64],request[128];
        memcpy(value,message->payload,(size_t)message->payloadlen);value[message->payloadlen]=0;
        if(strcmp(value,"disabled")){
            unsigned fields[4];
            if(strncmp(value,"active ",7)||!tvb_parse_schedule(value+7,fields)){
                fprintf(stderr,"mqtt_bridge: ignored invalid thermostat schedule\n");return;
            }
        }
        snprintf(request,sizeof(request),"SCHEDULE %s %s\n",endpoint_id,value);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected thermostat schedule %s\n",value);
        return;
    }
    if(kind==FEEDBACK_POSITION){
        if(message->payloadlen<=0||message->payloadlen>3)return;
        char value_text[8];memcpy(value_text,message->payload,(size_t)message->payloadlen);value_text[message->payloadlen]=0;
        char *end=0;long value=strtol(value_text,&end,10);
        if(end==value_text||*end||value<0||value>100){fprintf(stderr,"mqtt_bridge: ignored invalid cover position\n");return;}
        char request[80];snprintf(request,sizeof(request),"POSITION %s %ld\n",endpoint_id,value);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected cover position %ld\n",value);
        return;
    }
    if(kind==FEEDBACK_HS){
        char value[16];uint32_t hue,saturation;
        if(message->payloadlen<=0||message->payloadlen>15)return;
        memcpy(value,message->payload,(size_t)message->payloadlen);value[message->payloadlen]=0;
        const char *cursor=value;
        if(!tvb_parse_decimal(&cursor,359,&hue)||!isspace((unsigned char)*cursor)){
            fprintf(stderr,"mqtt_bridge: ignored invalid HS feedback\n");return;
        }
        while(isspace((unsigned char)*cursor))cursor++;
        if(!tvb_parse_decimal(&cursor,255,&saturation)||*cursor){
            fprintf(stderr,"mqtt_bridge: ignored invalid HS feedback\n");return;
        }
        char request[80];snprintf(request,sizeof(request),"HS %s %u %u\n",endpoint_id,
            (unsigned)hue,(unsigned)saturation);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected HS feedback\n");
        return;
    }
    if(kind==FEEDBACK_LEVEL||kind==FEEDBACK_COLOR_TEMP){
        if(message->payloadlen<=0||message->payloadlen>(kind==FEEDBACK_LEVEL?3:5))return;
        char value_text[8];
        if(message->payloadlen>=(int)sizeof(value_text))return;
        memcpy(value_text,message->payload,(size_t)message->payloadlen);value_text[message->payloadlen]=0;
        char *end=0;long value=strtol(value_text,&end,10);
        long minimum=kind==FEEDBACK_LEVEL?0:1000,maximum=kind==FEEDBACK_LEVEL?100:10000;
        if(end==value_text||*end||value<minimum||value>maximum){fprintf(stderr,"mqtt_bridge: ignored invalid numeric feedback\n");return;}
        if(kind==FEEDBACK_COLOR_TEMP){
            long normalized=normalize_color_temperature(value);
            bridge_debug("feedback uid=%s property=COLOR_TEMP raw=%ld normalized=%ld\n",
                endpoint_id,value,normalized);
            value=normalized;
        }
        char request[80];snprintf(request,sizeof(request),kind==FEEDBACK_LEVEL?"LEVEL %s %ld\n":"COLOR_TEMP %s %ld\n",endpoint_id,value);
        if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected numeric feedback %ld\n",value);
        return;
    }
    int state;
    if(!parse_state(message->payload,message->payloadlen,&state)){
        fprintf(stderr,"mqtt_bridge: ignored invalid state feedback\n");return;
    }
    char request[64];snprintf(request,sizeof(request),"SET %s %d\n",endpoint_id,state);
    if(!feedback_request(request))fprintf(stderr,"mqtt_bridge: provider rejected state %d\n",state);
}

static void mqtt_message(struct mosquitto *client,void *userdata,const struct mosquitto_message *message){
    pthread_mutex_lock(&registry_lock);
    if(!strcmp(message->topic,topic_management))management_locked(message);
    else device_message_locked(client,userdata,message);
    pthread_mutex_unlock(&registry_lock);
}
static void mqtt_connect(struct mosquitto *client,void *userdata,int result){
    (void)userdata;
    if(result){fprintf(stderr,"mqtt_bridge: MQTT connect failed: %s\n",mosquitto_connack_string(result));return;}
    const char *suffixes[]={"state","level","color_temperature","hs","position","target_temperature","current_temperature","hvac_mode","timer","schedule"};
    for(size_t i=0;i<sizeof(suffixes)/sizeof(*suffixes);i++){
        char topic[TEXT_SIZE*2];snprintf(topic,sizeof(topic),"%s/device/+/%s/set",config.topic_prefix,suffixes[i]);
        mosquitto_subscribe(client,NULL,topic,1);
    }
    mosquitto_subscribe(client,NULL,topic_management,1);
    mosquitto_publish(client,NULL,topic_availability,6,"online",1,true);
    pthread_mutex_lock(&registry_lock);publish_registry_locked();pthread_mutex_unlock(&registry_lock);
}
static int build_topics(void){
    return snprintf(topic_availability,sizeof(topic_availability),"%s/bridge/availability",config.topic_prefix)<(int)sizeof(topic_availability)&&
        snprintf(topic_info,sizeof(topic_info),"%s/bridge/info",config.topic_prefix)<(int)sizeof(topic_info)&&
        snprintf(topic_registry,sizeof(topic_registry),"%s/bridge/registry",config.topic_prefix)<(int)sizeof(topic_registry)&&
        snprintf(topic_management,sizeof(topic_management),"%s/management/request",config.topic_prefix)<(int)sizeof(topic_management);
}
static void watch_provider(void){
    while(running){
        int fd=provider_connect();
        if(fd<0){sleep(1);continue;}
        if(send(fd,"WATCH\n",6,MSG_NOSIGNAL)!=6){close(fd);sleep(1);continue;}
        char message[4096];ssize_t length=recv(fd,message,sizeof(message)-1,MSG_TRUNC);
        if(length<=0||length>=(ssize_t)sizeof(message)||memchr(message,0,(size_t)length)){close(fd);sleep(1);continue;}
        message[length]=0;
        cJSON *ack=cJSON_ParseWithOpts(message,NULL,1);
        int watching=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ack,"ok"));
        cJSON_Delete(ack);
        if(!watching){close(fd);sleep(1);continue;}
        pthread_mutex_lock(&registry_lock);reconcile_provider_locked();publish_registry_locked();pthread_mutex_unlock(&registry_lock);
        struct timeval no_timeout={0,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&no_timeout,sizeof(no_timeout));
        time_t retry=time(NULL)+5;
        unsigned health_failures=0;
        while(running){
            pthread_mutex_lock(&registry_lock);
            if(finish_units_locked())publish_registry_locked();
            resync_screens_locked();
            if(time(NULL)>=retry){
                cJSON *status=provider_query("GET");
                const cJSON *generation=cJSON_GetObjectItemCaseSensitive(status,"generation");
                int responsive=status&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(status,"ok"))&&
                    cJSON_IsNumber(generation);
                int healthy=responsive&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(status,"connected"));
                int changed=!healthy||
                    !cJSON_IsNumber(generation)||generation->valuedouble!=provider_generation;
                cJSON_Delete(status);
                if(!healthy){
                    health_failures++;
                    if(health_failures>=3){
                        fprintf(stderr,"mqtt_bridge: provider %s\n",responsive?
                            "control is responsive but AHA transport is disconnected":
                            "control channel is unresponsive");
                        pthread_mutex_unlock(&registry_lock);
                        break;
                    }
                }else health_failures=0;
                if(changed||(!provider_ready&&!unit_due)){reconcile_provider_locked();publish_registry_locked();}
                retry=time(NULL)+5;
            }
            pthread_mutex_unlock(&registry_lock);
            struct pollfd watched={fd,POLLIN,0};int ready=poll(&watched,1,1000);
            if(ready<0){if(errno==EINTR)continue;break;}
            if(!ready)continue;
            if(!(watched.revents&POLLIN))break;
            length=recv(fd,message,sizeof(message)-1,MSG_TRUNC);
            if(length<=0||length>=(ssize_t)sizeof(message))break;
            message[length]=0;cJSON *event=cJSON_Parse(message);
            const char *uid=json_string(event,"endpoint");
            pthread_mutex_lock(&registry_lock);
            const tvb_device *device=uid?tvb_registry_find(&registry,uid):NULL;
            if(device&&device->enabled&&provider_ready){
                char topic[TEXT_SIZE*2];snprintf(topic,sizeof(topic),"%s/device/%s/command",config.topic_prefix,device->uid);
                uint64_t sequence=0;int has_sequence=json_integer(
                    cJSON_GetObjectItemCaseSensitive(event,"sequence"),&sequence);
                int result=mosquitto_publish(mqtt,NULL,topic,(int)length,message,1,false);
                if(has_sequence)bridge_debug(
                    "command uid=%s sequence=%llu bytes=%zd mqtt_result=%d\n",
                    device->uid,(unsigned long long)sequence,length,result);
                else bridge_debug("command uid=%s sequence=invalid bytes=%zd mqtt_result=%d\n",
                    device->uid,length,result);
            }
            pthread_mutex_unlock(&registry_lock);cJSON_Delete(event);
        }
        close(fd);pthread_mutex_lock(&registry_lock);provider_ready=0;unit_due=0;publish_registry_locked();pthread_mutex_unlock(&registry_lock);
        if(running)sleep(1);
    }
}
static int create_registry_directory(void){
    char directory[TEXT_SIZE];strcpy(directory,config.registry_file);
    char *last=strrchr(directory,'/');if(!last)return 1;
    *last=0;
    for(char *p=directory+1;;p++){
        if(*p&&*p!='/')continue;
        char saved=*p;*p=0;
        if(*directory&&mkdir(directory,0700)&&errno!=EEXIST)return 0;
        struct stat st;if(*directory&&(stat(directory,&st)||!S_ISDIR(st.st_mode)))return 0;
        *p=saved;if(!saved)break;
    }
    return 1;
}
int main(int argc,char **argv){
    const char *path=argc>1?argv[1]:"/var/tmp/tentahacle-mqtt.conf";
    debug_enabled=debug_requested(getenv("TENTAHACLE_DEBUG"));
    if(argc>2||!read_config(path)||!build_topics())return 2;
    char error[256];
    if(access(config.registry_file,F_OK)&&errno==ENOENT){
        if(!create_registry_directory())return 2;
        tvb_registry_init(&registry);
        if(!tvb_registry_save_file(config.registry_file,&registry,error,sizeof(error))){fprintf(stderr,"mqtt_bridge: %s\n",error);return 2;}
        if(!persist_flash()){flash_dirty=1;fprintf(stderr,"mqtt_bridge: initial flash persistence failed\n");}
    }else if(!tvb_registry_load_file(config.registry_file,&registry,error,sizeof(error))){fprintf(stderr,"mqtt_bridge: %s\n",error);return 2;}
    signal(SIGINT,stop_handler);signal(SIGTERM,stop_handler);signal(SIGPIPE,SIG_IGN);
    int result=mosquitto_lib_init();
    if(result!=MOSQ_ERR_SUCCESS)return 3;
    mqtt=mosquitto_new(config.client_id,true,NULL);
    if(!mqtt){mosquitto_lib_cleanup();return 3;}
    if(*config.username||*config.password)result=mosquitto_username_pw_set(mqtt,config.username,config.password);
    if(result==MOSQ_ERR_SUCCESS)result=mosquitto_will_set(mqtt,topic_availability,7,"offline",1,true);
    mosquitto_connect_callback_set(mqtt,mqtt_connect);mosquitto_message_callback_set(mqtt,mqtt_message);
    if(result==MOSQ_ERR_SUCCESS)result=mosquitto_reconnect_delay_set(mqtt,1,30,true);
    if(result==MOSQ_ERR_SUCCESS)result=mosquitto_connect_async(mqtt,config.host,config.port,30);
    if(result==MOSQ_ERR_SUCCESS)result=mosquitto_loop_start(mqtt);
    if(result!=MOSQ_ERR_SUCCESS){
        fprintf(stderr,"mqtt_bridge: MQTT startup failed: %s\n",mosquitto_strerror(result));
        mosquitto_destroy(mqtt);mosquitto_lib_cleanup();return 4;
    }
    watch_provider();mosquitto_publish(mqtt,NULL,topic_availability,7,"offline",1,true);
    mosquitto_disconnect(mqtt);mosquitto_loop_stop(mqtt,true);mosquitto_destroy(mqtt);mosquitto_lib_cleanup();return 0;
}
