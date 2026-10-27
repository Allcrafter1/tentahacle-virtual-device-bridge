/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Host test: compile with -ffunction-sections -Wl,--gc-sections, cJSON and
 * device_registry.c/registry_store.c. No broker or live Provider is used. */
#define main mqtt_bridge_program_main
#include "../freetz/package/src/mqtt_bridge.c"
#undef main
#include <assert.h>

static char published_response[16384];
static unsigned allocation_calls,allocation_failure,live_allocations;
static void debug_logging(void){
    assert(!debug_requested(NULL)&&!debug_requested("")&&!debug_requested("no"));
    assert(debug_requested("1")&&debug_requested("yes")&&debug_requested("TRUE"));
    FILE *capture=tmpfile();assert(capture);int saved=dup(STDERR_FILENO);assert(saved>=0);
    fflush(stderr);assert(dup2(fileno(capture),STDERR_FILENO)==STDERR_FILENO);
    debug_enabled=0;bridge_debug("hidden-marker\n");
    debug_enabled=1;debug_feedback("LEVEL","TVB0123456789ABCDEF",
        "LEVEL TVB0123456789ABCDEF 55\n","accepted");
    fflush(stderr);assert(dup2(saved,STDERR_FILENO)==STDERR_FILENO);close(saved);
    rewind(capture);char output[512]={0};size_t length=fread(output,1,sizeof(output)-1,capture);
    assert(length>0&&!strstr(output,"hidden-marker"));
    assert(strstr(output,"uid=TVB0123456789ABCDEF")&&strstr(output,"property=LEVEL")&&
        strstr(output,"value=55")&&strstr(output,"provider=accepted"));
    fclose(capture);debug_enabled=0;
}
static void *failing_malloc(size_t size){
    if(++allocation_calls==allocation_failure)return NULL;
    void *memory=malloc(size);if(memory)++live_allocations;return memory;
}
static void tracked_free(void *memory){if(memory)--live_allocations;free(memory);}
int mosquitto_publish(struct mosquitto *client,int *mid,const char *topic,int length,const void *payload,int qos,bool retain){
    (void)client;(void)mid;(void)qos;(void)retain;
    if(strstr(topic,"/management/response/")){
        assert(length<(int)sizeof(published_response));memcpy(published_response,payload,(size_t)length);published_response[length]=0;
    }
    return MOSQ_ERR_SUCCESS;
}
static const char *candidate(const char *json,tvb_device_registry *r,int *mutate,int *announce){
    cJSON *root=cJSON_Parse(json);assert(root);
    const char *error=management_candidate(root,r,mutate,announce);cJSON_Delete(root);return error;
}
static void management(const char *payload){
    struct mosquitto_message message={.topic=topic_management,.payload=(void*)payload,.payloadlen=(int)strlen(payload)};
    management_locked(&message);
}
static void feedback(const char *uid,const char *kind,const char *value){
    char topic[512];snprintf(topic,sizeof(topic),"%s/device/%s/%s/set",config.topic_prefix,uid,kind);
    struct mosquitto_message message={.topic=topic,.payload=(void*)value,.payloadlen=(int)strlen(value)};
    device_message_locked(NULL,NULL,&message);
}
static long cached_numeric_feedback(size_t row,enum feedback_kind kind){
    char operation[24],uid[20];long value=0;
    assert(sscanf(feedback_cache[row][kind],"%23s %19s %ld",operation,uid,&value)==3);
    return value;
}
static void color_temperature_palette_feedback(void){
    static const struct {const char *input;long expected;} cases[]={
        {"2702",2700},{"3003",3000},{"4716",4700},{"6535",6500},
        {"2850",2700},{"3200",3000},{"1000",2700},{"10000",6500}
    };
    tvb_registry_init(&registry);provider_ready=0;
    memset(feedback_cache,0,sizeof(feedback_cache));
    assert(tvb_registry_add(&registry,registry.revision,"TVB0123456789ABCDEF",
        "Kelvin",TVB_PROFILE_COLOR_TEMPERATURE_LIGHT,NULL)==TVB_REGISTRY_OK);
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);i++){
        feedback(registry.devices[0].uid,"color_temperature",cases[i].input);
        assert(cached_numeric_feedback(0,FEEDBACK_COLOR_TEMP)==cases[i].expected);
    }
    feedback(registry.devices[0].uid,"color_temperature","bad");
    assert(cached_numeric_feedback(0,FEEDBACK_COLOR_TEMP)==6500);
    feedback(registry.devices[0].uid,"color_temperature","999");
    assert(cached_numeric_feedback(0,FEEDBACK_COLOR_TEMP)==6500);
    feedback(registry.devices[0].uid,"color_temperature","10001");
    assert(cached_numeric_feedback(0,FEEDBACK_COLOR_TEMP)==6500);
    memset(feedback_cache,0,sizeof(feedback_cache));
}
/* Local fake Provider validates identity checks and delayed provisioning;
 * it never opens a network socket or contacts an AVM daemon. */
static pid_t fake_provider(const char *path){
    int listener=socket(AF_UNIX,SOCK_SEQPACKET,0);assert(listener>=0);
    struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
    assert(!bind(listener,(struct sockaddr*)&address,sizeof(address))&&!listen(listener,8));
    pid_t child=fork();assert(child>=0);
    if(!child){
        unsigned id=456,renamed=0,units=0,target=40,heat_target=40;
        int actual=-9999,conflict=0,present=1,remove_failure=0;
        for(;;){
            int fd=accept(listener,NULL,NULL);if(fd<0)_exit(2);
            char command[256],reply[512];ssize_t n=recv(fd,command,sizeof(command)-1,0);
            if(n<=0){close(fd);continue;}command[n]=0;
            if(!strncmp(command,"RESTORE ",8)){
                char uid[20];unsigned restored;
                assert(sscanf(command,"RESTORE %19s %u",uid,&restored)==2);
                assert(restored==456);present=1;
            }
            if(!strcmp(command,"TEST REMOVE_FAILURE"))remove_failure=1;
            if(!strcmp(command,"TEST REMOVE_OK"))remove_failure=0;
            if(!strncmp(command,"REMOVE ",7))present=0;
            if(!strcmp(command,"TEST WRONG_ID"))id=999;
            if(!strcmp(command,"TEST NAME_CONFLICT")){id=456;conflict=1;}
            if(!strncmp(command,"RENAME ",7)){conflict=0;renamed++;}
            if(!strncmp(command,"UNIT ",5))units++;
            if(!strncmp(command,"SCHEDULE ",9))target=40;
            if(!strncmp(command,"TARGET ",7)){
                if(strstr(command," max"))heat_target=254;
                else {double value=0;assert(sscanf(strrchr(command,' ')+1,"%lf",&value)==1);heat_target=(unsigned)(value*2+0.5);}
                if(target!=253)target=heat_target;
            }
            if(!strncmp(command,"ACTUAL ",7)){
                if(strstr(command," unknown"))actual=-9999;
                else {double value=0;assert(sscanf(strrchr(command,' ')+1,"%lf",&value)==1);actual=(int)(value*10+(value>=0?0.5:-0.5));}
            }
            if(!strncmp(command,"MODE ",5)&&strstr(command," off"))target=253;
            if(!strncmp(command,"MODE ",5)&&strstr(command," heat"))target=heat_target;
            snprintf(reply,sizeof(reply),"{\"ok\":%s,\"connected\":true,\"generation\":7,\"remote_id\":%u,\"profile\":\"color_temperature_light\",\"renamed\":%u,\"units\":%u,\"target\":%u,\"actual\":%d,\"present\":%s}",
                (conflict&&!strncmp(command,"RESTORE ",8))||(remove_failure&&!strncmp(command,"REMOVE ",7))?"false":"true",id,renamed,units,target,actual,present?"true":"false");
            (void)send(fd,reply,strlen(reply),MSG_NOSIGNAL);close(fd);
        }
    }
    close(listener);return child;
}
static void profile_matrix_and_cache(void){
    static const char *kinds[]={"state","level","color_temperature","hs","position","target_temperature","current_temperature","hvac_mode","timer","schedule"};
    static const char *values[]={"ON","55","2700","120 100","60","20.5","21.3","heat","boost 2000","active 40 36 50 60"};
    static const unsigned allowed[]={1u,3u,7u,1u<<4,(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)};
    tvb_registry_init(&registry);provider_ready=0;
    for(unsigned profile=0;profile<TVB_PROFILE_COUNT;profile++){
        char uid[20];snprintf(uid,sizeof(uid),"TVB%016X",profile+1);
        assert(tvb_registry_add(&registry,registry.revision,uid,"Matrix",(tvb_device_profile)profile,NULL)==TVB_REGISTRY_OK);
        for(unsigned kind=0;kind<FEEDBACK_COUNT;kind++){
            memset(feedback_cache,0,sizeof(feedback_cache));
            feedback(uid,kinds[kind],values[kind]);
            int cached=0;for(unsigned field=0;field<FEEDBACK_COUNT;field++)cached|=*feedback_cache[profile][field]!=0;
            assert(cached==((allowed[profile]&(1u<<kind))!=0));
        }
    }
    const char *color_uid=registry.devices[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT].uid;
    assert(tvb_registry_set_color_full(&registry,registry.revision,color_uid,1)==TVB_REGISTRY_OK);
    feedback(color_uid,"hs","120 100");
    assert(strstr(feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_HS],"120 100"));
    feedback(color_uid,"hs","359 255");
    assert(strstr(feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_HS],"359 255"));
    feedback(color_uid,"hs","999999999999999");
    feedback(color_uid,"hs","1 999999999999");
    feedback(color_uid,"hs","-1 2");
    feedback(color_uid,"hs","1 2 trailing");
    feedback(color_uid,"hs","360 0");
    feedback(color_uid,"hs","0 256");
    assert(strstr(feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_HS],"359 255"));
    feedback(color_uid,"color_temperature","3000");
    assert(!*feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_HS]);
    assert(strstr(feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_COLOR_TEMP],"3000"));
    feedback(color_uid,"hs","240 200");
    assert(!*feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_COLOR_TEMP]);
    feedback(color_uid,"hs","");
    assert(!*feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_HS]);
    feedback(color_uid,"hs","120 100");
    tvb_device_registry capability_before=registry;
    assert(tvb_registry_set_color_full(&registry,registry.revision,color_uid,0)==TVB_REGISTRY_OK);
    remap_feedback_cache(&capability_before);
    assert(!*feedback_cache[TVB_PROFILE_COLOR_TEMPERATURE_LIGHT][FEEDBACK_HS]);
    tvb_device_registry before=registry;
    for(unsigned i=0;i<TVB_PROFILE_COUNT;i++)snprintf(feedback_cache[i][0],128,"row%u",i);
    assert(tvb_registry_remove(&registry,registry.revision,before.devices[0].uid)==TVB_REGISTRY_OK);
    assert(tvb_registry_remove(&registry,registry.revision,before.devices[2].uid)==TVB_REGISTRY_OK);
    remap_feedback_cache(&before);
    assert(!strcmp(feedback_cache[0][0],"row1"));
    assert(!strcmp(feedback_cache[1][0],"row3"));
    assert(!strcmp(feedback_cache[2][0],"row4"));
    assert(!*feedback_cache[3][0]);
    before=registry;
    assert(tvb_registry_add(&registry,registry.revision,"TVB0000000000000099","New",TVB_PROFILE_SWITCH,NULL)==TVB_REGISTRY_OK);
    remap_feedback_cache(&before);assert(!*feedback_cache[3][0]);
    memset(feedback_cache,0,sizeof(feedback_cache));
}
static void management_capacity(void){
    tvb_device_registry candidate_registry,before;char uid[20],request[320];
    int mutate,announce;
    tvb_registry_init(&candidate_registry);
    for(size_t i=0;i<TVB_REGISTRY_CAPACITY;i++){
        snprintf(uid,sizeof(uid),"TVB%016llX",(unsigned long long)i);
        snprintf(request,sizeof(request),
            "{\"schema_version\":1,\"request_id\":\"capacity\",\"operation\":\"upsert_device\","
            "\"device\":{\"uid\":\"%s\",\"name\":\"Device\",\"profile\":\"switch\"}}",uid);
        assert(!candidate(request,&candidate_registry,&mutate,&announce));
        assert(mutate&&candidate_registry.count==i+1);
    }
    assert(candidate_registry.count==64);
    before=candidate_registry;
    assert(!strcmp(candidate(
        "{\"schema_version\":1,\"request_id\":\"overflow\",\"operation\":\"upsert_device\","
        "\"device\":{\"uid\":\"TVBFFFFFFFFFFFFFFFF\",\"name\":\"Overflow\",\"profile\":\"switch\"}}",
        &candidate_registry,&mutate,&announce),"registry_full"));
    assert(!memcmp(&candidate_registry,&before,sizeof(before)));
}
int main(void){
    debug_logging();
    color_temperature_palette_feedback();
    profile_matrix_and_cache();
    management_capacity();
    pid_t timed_child=fork();assert(timed_child>=0);
    if(!timed_child){for(;;)pause();}
    assert(!wait_child(timed_child,1,0));
    assert(waitpid(timed_child,NULL,WNOHANG)==-1&&errno==ECHILD);
    pid_t stopped_child=fork();assert(stopped_child>=0);
    if(!stopped_child){for(;;)pause();}
    pid_t stop_signal=fork();assert(stop_signal>=0);
    if(!stop_signal){struct timespec delay={0,100000000};nanosleep(&delay,NULL);kill(getppid(),SIGTERM);_exit(0);}
    signal(SIGTERM,stop_handler);running=1;
    struct timespec stop_started,stop_finished;
    assert(!clock_gettime(CLOCK_MONOTONIC,&stop_started));
    assert(!wait_child(stopped_child,30,1));
    assert(!clock_gettime(CLOCK_MONOTONIC,&stop_finished));
    assert(stop_finished.tv_sec-stop_started.tv_sec<3);
    assert(waitpid(stopped_child,NULL,WNOHANG)==-1&&errno==ECHILD);
    assert(waitpid(stop_signal,NULL,0)==stop_signal);signal(SIGTERM,SIG_DFL);running=1;
    pid_t flash_child=fork();assert(flash_child>=0);
    if(!flash_child){struct timespec delay={0,100000000};nanosleep(&delay,NULL);_exit(0);}
    running=0;assert(wait_child(flash_child,2,0));running=1;
    assert(identifier_valid("bridge_7530-1",32)&&!identifier_valid("bridge/7530",32));
    assert(!identifier_valid("ab",32)&&!identifier_valid("Bridge",32));
    assert(!identifier_valid("123456789012345678901234567890123",32));
    assert(prefix_valid("tentahacle/bridge_7530-1"));
    assert(!prefix_valid("/bridge")&&!prefix_valid("bridge/")&&!prefix_valid("bridge//device")&&!prefix_valid("bridge/+"));
    int mutate,announce;tvb_device_registry r;tvb_registry_init(&r);
    const char *add="{\"schema_version\":1,\"request_id\":\"test-1\",\"operation\":\"upsert_device\",\"device\":{\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"Licht Küche\",\"profile\":\"color_temperature_light\",\"enabled\":true}}";
    assert(!candidate(add,&r,&mutate,&announce));assert(mutate&&r.count==1&&r.devices[0].remote_id==456);
    uint64_t revision=r.revision;
    assert(!candidate(add,&r,&mutate,&announce));assert(r.revision==revision);
    const char *full="{\"schema_version\":1,\"request_id\":\"test-full\",\"operation\":\"upsert_device\",\"device\":{\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"Licht Küche\",\"profile\":\"color_temperature_light\",\"enabled\":true,\"color_full\":true}}";
    assert(!candidate(full,&r,&mutate,&announce));assert(mutate&&r.devices[0].color_full);
    revision=r.revision;
    assert(!candidate(add,&r,&mutate,&announce));assert(r.revision==revision&&r.devices[0].color_full);
    assert(candidate("{\"schema_version\":1,\"operation\":\"upsert_device\",\"device\":{\"uid\":\"TVB0123456789ABCDE0\",\"name\":\"Bad\",\"profile\":\"switch\",\"color_full\":false}}",&r,&mutate,&announce));
    assert(candidate("{\"schema_version\":2,\"operation\":\"list_devices\"}",&r,&mutate,&announce));
    assert(candidate("{\"schema_version\":1,\"operation\":\"delete_device\"}",&r,&mutate,&announce));
    assert(candidate("{\"schema_version\":1,\"schema_version\":1,\"operation\":\"list_devices\"}",&r,&mutate,&announce));
    assert(candidate("{\"schema_version\":1,\"operation\":\"rename_device\",\"expected_revision\":0,\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"bad\"}",&r,&mutate,&announce));
    assert(candidate("{\"schema_version\":1,\"operation\":\"upsert_device\",\"device\":{\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"bad\",\"profile\":\"switch\"}}",&r,&mutate,&announce));
    assert(candidate("{\"schema_version\":1,\"operation\":\"reconcile_devices\",\"enabled_uids\":[\"TVB0123456789ABCDEF\",\"TVB0123456789abcdef\"]}",&r,&mutate,&announce));
    assert(!candidate("{\"schema_version\":1,\"operation\":\"reconcile_devices\",\"enabled_uids\":[]}",&r,&mutate,&announce));
    assert(!r.devices[0].enabled&&r.count==1);
    assert(!candidate(add,&r,&mutate,&announce));assert(r.devices[0].enabled);
    tvb_device_registry pruned=r;
    assert(!candidate("{\"schema_version\":1,\"operation\":\"prune_devices\",\"keep_uids\":[\"TVB0123456789ABCDEF\",\"TVBFFFFFFFFFFFFFFFF\"]}",&pruned,&mutate,&announce));
    assert(pruned.count==1);
    assert(candidate("{\"schema_version\":1,\"operation\":\"prune_devices\",\"keep_uids\":[\"TVBFFFFFFFFFFFFFFFF\",\"TVBffffffffffffffff\"]}",&pruned,&mutate,&announce));
    pruned=r;
    assert(!candidate("{\"schema_version\":1,\"operation\":\"prune_devices\",\"keep_uids\":[\"TVBFFFFFFFFFFFFFFFF\"]}",&pruned,&mutate,&announce));
    assert(pruned.count==0);
    pruned=r;
    assert(!candidate("{\"schema_version\":1,\"operation\":\"prune_devices\",\"keep_uids\":[]}",&pruned,&mutate,&announce));
    assert(mutate&&pruned.count==0&&pruned.next_remote_id==457);
    assert(!request_id_valid("../../bad")&&!request_id_valid("bad/+"));assert(request_id_valid("id-012_abc"));
    cJSON *nan=cJSON_CreateNumber(NAN);uint64_t integer;assert(!json_integer(nan,&integer));cJSON_Delete(nan);
    cJSON *infinite=cJSON_CreateNumber(INFINITY);assert(!json_integer(infinite,&integer));cJSON_Delete(infinite);
    registry=r;provider_ready=0;
    cJSON_Hooks hooks={failing_malloc,tracked_free};
    cJSON_InitHooks(&hooks);
    for(allocation_failure=1;allocation_failure<256;allocation_failure++){
        allocation_calls=0;
        management("{\"schema_version\":1,\"request_id\":\"oom\",\"operation\":\"list_devices\"}");
        assert(!live_allocations);
    }
    cJSON_InitHooks(NULL);
    feedback(r.devices[0].uid,"level","55");assert(strstr(feedback_cache[0][1],"55"));
    feedback(r.devices[0].uid,"level","101");assert(strstr(feedback_cache[0][1],"55"));
    feedback(r.devices[0].uid,"position","40");assert(!*feedback_cache[0][FEEDBACK_POSITION]);
    feedback(r.devices[0].uid,"target_temperature","nan");assert(!*feedback_cache[0][FEEDBACK_TARGET]);
    feedback(r.devices[0].uid,"color_temperature","2700");assert(strstr(feedback_cache[0][2],"2700"));
    assert(tvb_registry_add(&registry,registry.revision,"TVB1123456789ABCDEF","Thermo",TVB_PROFILE_THERMOSTAT,NULL)==TVB_REGISTRY_OK);
    feedback(registry.devices[1].uid,"target_temperature","nan");assert(!*feedback_cache[1][FEEDBACK_TARGET]);
    feedback(registry.devices[1].uid,"target_temperature","inf");assert(!*feedback_cache[1][FEEDBACK_TARGET]);
    feedback(registry.devices[1].uid,"target_temperature","20.5");assert(strstr(feedback_cache[1][FEEDBACK_TARGET],"20.5"));
    feedback(registry.devices[1].uid,"target_temperature","20.2");assert(strstr(feedback_cache[1][FEEDBACK_TARGET],"20.5"));
    feedback(registry.devices[1].uid,"target_temperature","max");assert(strstr(feedback_cache[1][FEEDBACK_TARGET],"max"));
    feedback(registry.devices[1].uid,"current_temperature","nan");assert(!*feedback_cache[1][FEEDBACK_ACTUAL]);
    feedback(registry.devices[1].uid,"current_temperature","21.3");assert(strstr(feedback_cache[1][FEEDBACK_ACTUAL],"21.3"));
    feedback(registry.devices[1].uid,"current_temperature","unknown");assert(strstr(feedback_cache[1][FEEDBACK_ACTUAL],"unknown"));
    feedback(registry.devices[1].uid,"current_temperature","21.34");assert(strstr(feedback_cache[1][FEEDBACK_ACTUAL],"unknown"));
    feedback(registry.devices[1].uid,"current_temperature","-50.0");assert(strstr(feedback_cache[1][FEEDBACK_ACTUAL],"-50.0"));
    feedback(registry.devices[1].uid,"current_temperature","100.1");assert(strstr(feedback_cache[1][FEEDBACK_ACTUAL],"-50.0"));
    feedback(registry.devices[1].uid,"timer","boost 4294967296");assert(!*feedback_cache[1][FEEDBACK_TIMER]);
    feedback(registry.devices[1].uid,"timer","boost 9999999999999999999999999");assert(!*feedback_cache[1][FEEDBACK_TIMER]);
    feedback(registry.devices[1].uid,"schedule","active 4294967336 36 50 60");assert(!*feedback_cache[1][FEEDBACK_SCHEDULE]);
    feedback(registry.devices[1].uid,"schedule","active 40 36 50 60");assert(strstr(feedback_cache[1][FEEDBACK_SCHEDULE],"active"));
    char directory[]="/tmp/tvb-mqtt-test-XXXXXX";assert(mkdtemp(directory));
    /* Native AHA helpers must not inherit the bridge's Freetz library path.
     * Exercise the actual fork/exec boundary for both native operations. */
    char native_helper[512],saved_helper[sizeof(config.aha_control)];
    snprintf(native_helper,sizeof(native_helper),"%s/native-helper",directory);
    FILE *helper=fopen(native_helper,"w");assert(helper);
    assert(fputs("#!/bin/sh\n[ -z \"${LD_LIBRARY_PATH+x}\" ] || exit 91\n"
                 "case \"$1\" in delete) [ \"$#\" -eq 2 ] ;; resync) [ \"$#\" -eq 3 ] ;; *) exit 92 ;; esac\n",helper)>=0);
    assert(!fclose(helper)&&!chmod(native_helper,0700));
    strcpy(saved_helper,config.aha_control);
    assert(copy_value(config.aha_control,sizeof(config.aha_control),native_helper));
    const char *old_library_path=getenv("LD_LIBRARY_PATH");
    char *saved_library_path=old_library_path?strdup(old_library_path):NULL;
    assert(!old_library_path||saved_library_path);
    assert(!setenv("LD_LIBRARY_PATH","/freetz-test-libraries",1));
    assert(native_delete(registry.devices[0].uid));
    assert(native_resync(&registry.devices[0]));
    assert(!strcmp(getenv("LD_LIBRARY_PATH"),"/freetz-test-libraries"));
    if(saved_library_path){assert(!setenv("LD_LIBRARY_PATH",saved_library_path,1));free(saved_library_path);}
    else assert(!unsetenv("LD_LIBRARY_PATH"));
    strcpy(config.aha_control,saved_helper);assert(!unlink(native_helper));
    snprintf(config.registry_file,sizeof(config.registry_file),"%s/registry.json",directory);
    strcpy(config.control_socket,"/tmp/nonexistent-tvb-mqtt-test.sock");config.persist_command[0]=0;strcpy(config.bridge_id,"test");assert(build_topics());
    tvb_registry_init(&registry);
    management(add);
    assert(registry.count==1);
    cJSON *response=cJSON_Parse(published_response);assert(cJSON_IsTrue(cJSON_GetObjectItem(response,"accepted")));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(response,"ready")));cJSON_Delete(response);
    char error[256];tvb_device_registry loaded;
    assert(tvb_registry_load_file(config.registry_file,&loaded,error,sizeof(error))&&loaded.count==1);
    management("{\"schema_version\": 1, \"request_id\": \"literal-name\", \"operation\": \"rename_device\", \"uid\": \"TVB0123456789ABCDEF\", \"name\": \"literal \\\\u0000\"}");
    assert(!strcmp(registry.devices[0].name,"literal \\u0000"));
    revision=registry.revision;
    management("{\"schema_version\": 1, \"request_id\": \"literal-name\", \"operation\": \"rename_device\", \"uid\": \"TVB0123456789ABCDEF\", \"name\": \"bad\\u0000suffix\"}");
    assert(registry.revision==revision);
    management(add);
    revision=registry.revision;
    management(add);assert(registry.revision==revision);
    strcpy(config.registry_file,"/this/path/does/not/exist/registry.json");
    management("{\"schema_version\":1,\"request_id\":\"rename\",\"operation\":\"rename_device\",\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"Changed\"}");
    assert(registry.revision==revision&&!strcmp(registry.devices[0].name,"Licht Küche"));
    response=cJSON_Parse(published_response);assert(cJSON_IsFalse(cJSON_GetObjectItem(response,"accepted")));cJSON_Delete(response);
    snprintf(config.registry_file,sizeof(config.registry_file),"%s/registry.json",directory);
    strcpy(config.persist_command,"/bin/false");
    const char *rename="{\"schema_version\":1,\"request_id\":\"rename\",\"operation\":\"rename_device\",\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"Changed\"}";
    management(rename);assert(flash_dirty&&!strcmp(registry.devices[0].name,"Changed"));
    response=cJSON_Parse(published_response);assert(cJSON_IsTrue(cJSON_GetObjectItem(response,"accepted")));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(response,"flash_persisted")));cJSON_Delete(response);
    strcpy(config.persist_command,"/bin/true");management(rename);assert(!flash_dirty);
    snprintf(config.control_socket,sizeof(config.control_socket),"%s/provider.sock",directory);
    pid_t server=fake_provider(config.control_socket);
    assert(reconcile_provider_locked()&&!provider_ready&&unit_due>time(NULL));
    assert(!finish_units_locked());
    unit_due=time(NULL)-1;assert(finish_units_locked()&&provider_ready);
    assert(screen_resync_due&&screen_resync_attempts[0]==SCREEN_RESYNC_ATTEMPTS);
    strcpy(config.aha_control,"/bin/false");screen_resync_due=time(NULL)-1;
    resync_screens_locked();assert(screen_resync_attempts[0]==SCREEN_RESYNC_ATTEMPTS-1&&screen_resync_due);
    strcpy(config.aha_control,"/bin/true");screen_resync_due=time(NULL)-1;
    resync_screens_locked();assert(!screen_resync_attempts[0]&&!screen_resync_due);
    /* Completed targets cannot be re-submitted by periodic watcher ticks. */
    resync_screens_locked();assert(!screen_resync_attempts[0]&&!screen_resync_due);
    screen_resync_attempts[0]=1;screen_resync_due=time(NULL)-1;
    strcpy(config.aha_control,"/bin/false");resync_screens_locked();
    assert(!screen_resync_attempts[0]&&!screen_resync_due);
    /* A not-ready target must not starve the next ready target. */
    tvb_device_registry before_resync=registry;
    assert(tvb_registry_add(&registry,registry.revision,"TVB0000000000000077","Other",TVB_PROFILE_DIMMABLE_LIGHT,NULL)==TVB_REGISTRY_OK);
    screen_resync_attempts[0]=screen_resync_attempts[1]=3;screen_resync_cursor=0;
    screen_resync_due=time(NULL)-1;resync_screens_locked();
    assert(screen_resync_attempts[0]==2&&screen_resync_attempts[1]==3);
    strcpy(config.aha_control,"/bin/true");screen_resync_due=time(NULL)-1;resync_screens_locked();
    assert(screen_resync_attempts[0]==2&&screen_resync_attempts[1]==0);
    screen_resync_due=time(NULL)-1;resync_screens_locked();
    assert(!screen_resync_attempts[0]&&!screen_resync_due);
    registry=before_resync;
    cJSON *status=provider_query("GET");assert(cJSON_GetObjectItem(status,"units")->valueint==1);cJSON_Delete(status);
    /* Provider SCHEDULE initializes its target: cached actual target/mode
     * must win on reconnect, including OFF with a retained active schedule. */
    tvb_device_registry before_replay=registry;
    registry.devices[0].profile=TVB_PROFILE_THERMOSTAT;
    memset(feedback_cache,0,sizeof(feedback_cache));provider_ready=0;
    feedback(registry.devices[0].uid,"schedule","active 40 36 50 60");
    feedback(registry.devices[0].uid,"current_temperature","21.3");
    feedback(registry.devices[0].uid,"target_temperature","21.5");
    feedback(registry.devices[0].uid,"hvac_mode","off");
    unit_due=time(NULL)-1;assert(finish_units_locked());
    status=provider_query("GET");assert(cJSON_GetObjectItem(status,"target")->valueint==253);
    assert(cJSON_GetObjectItem(status,"actual")->valueint==213);cJSON_Delete(status);
    feedback_cache[0][FEEDBACK_MODE][0]=0;unit_due=time(NULL)-1;assert(finish_units_locked());
    status=provider_query("GET");assert(cJSON_GetObjectItem(status,"target")->valueint==43);cJSON_Delete(status);
    feedback(registry.devices[0].uid,"target_temperature","max");
    feedback(registry.devices[0].uid,"hvac_mode","off");
    feedback(registry.devices[0].uid,"current_temperature","unknown");
    unit_due=time(NULL)-1;assert(finish_units_locked());
    status=provider_query("GET");assert(cJSON_GetObjectItem(status,"target")->valueint==253);
    assert(cJSON_GetObjectItem(status,"actual")->valueint==-9999);cJSON_Delete(status);
    feedback_cache[0][FEEDBACK_MODE][0]=0;
    snprintf(feedback_cache[0][FEEDBACK_MODE],128,"MODE %s heat\n",registry.devices[0].uid);
    unit_due=time(NULL)-1;assert(finish_units_locked());
    status=provider_query("GET");assert(cJSON_GetObjectItem(status,"target")->valueint==254);cJSON_Delete(status);
    registry=before_replay;memset(feedback_cache,0,sizeof(feedback_cache));
    assert(provider_request("TEST WRONG_ID"));assert(!reconcile_provider_locked()&&!provider_ready);
    assert(provider_request("TEST NAME_CONFLICT"));assert(reconcile_provider_locked());
    status=provider_query("GET");assert(cJSON_GetObjectItem(status,"renamed")->valueint==1);cJSON_Delete(status);
    strcpy(config.aha_control,"/bin/true");
    /* Failure at each deletion stage must leave desired state recoverable:
     * the native helper, REMOVE acknowledgement, or final registry save. */
    char saved_registry_file[sizeof(config.registry_file)];
    strcpy(saved_registry_file,config.registry_file);
    for(unsigned failure=0;failure<3;failure++){
        unit_due=time(NULL)-1;assert(finish_units_locked()&&provider_ready);
        if(failure==0)strcpy(config.registry_file,"/this/path/does/not/exist/registry.json");
        if(failure==1)strcpy(config.aha_control,"/bin/false");
        if(failure==2)assert(provider_request("TEST REMOVE_FAILURE"));
        management("{\"schema_version\":1,\"request_id\":\"failed-prune\",\"operation\":\"prune_devices\",\"keep_uids\":[]}");
        assert(registry.count==1);
        status=provider_query("GET");
        assert(cJSON_IsTrue(cJSON_GetObjectItem(status,"present"))==(failure==1));cJSON_Delete(status);
        response=cJSON_Parse(published_response);
        assert(cJSON_IsFalse(cJSON_GetObjectItem(response,"accepted")));
        assert(cJSON_IsFalse(cJSON_GetObjectItem(response,"ready")));cJSON_Delete(response);
        assert(!provider_ready&&!unit_due&&!screen_resync_due);
        assert(tvb_registry_load_file(saved_registry_file,&loaded,error,sizeof(error))&&loaded.count==1);
        strcpy(config.registry_file,saved_registry_file);strcpy(config.aha_control,"/bin/true");
        assert(provider_request("TEST REMOVE_OK"));
        assert(reconcile_provider_locked());
        unit_due=time(NULL)-1;assert(finish_units_locked()&&provider_ready);
        status=provider_query("GET");assert(cJSON_IsTrue(cJSON_GetObjectItem(status,"present")));cJSON_Delete(status);
    }
    management("{\"schema_version\":1,\"request_id\":\"prune\",\"operation\":\"prune_devices\",\"keep_uids\":[]}");
    assert(registry.count==0&&registry.next_remote_id==457);
    response=cJSON_Parse(published_response);assert(cJSON_IsTrue(cJSON_GetObjectItem(response,"accepted")));cJSON_Delete(response);
    assert(tvb_registry_load_file(config.registry_file,&loaded,error,sizeof(error))&&loaded.count==0&&loaded.next_remote_id==457);
    kill(server,SIGTERM);assert(waitpid(server,NULL,0)==server);unlink(config.control_socket);
    char file[512];snprintf(file,sizeof(file),"%s/registry.json",directory);unlink(file);rmdir(directory);
    puts("mqtt_bridge_test: OK");return 0;
}
