// SPDX-License-Identifier: MIT OR Apache-2.0
/* Small local client for native AHA lifecycle operations.  AVM libraries are
 * loaded at runtime and are neither linked into nor distributed by the
 * project. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *load_symbol(void *handle,const char *name){
    dlerror();
    void *symbol=dlsym(handle,name);
    const char *error=dlerror();
    if(error||!symbol){fprintf(stderr,"tentahacle-aha-control: %s\n",error?error:"missing symbol");return NULL;}
    return symbol;
}
static int valid_uid(const char *uid){
    if(!uid||strlen(uid)!=19||memcmp(uid,"TVB",3))return 0;
    for(size_t i=3;i<19;i++)
        if(!((uid[i]>='0'&&uid[i]<='9')||(uid[i]>='A'&&uid[i]<='F')))return 0;
    return 1;
}
/* Readiness is checked before any write. A successful submission is not a
 * DECT delivery acknowledgement: the native setter has no return value. */
static const char resync_script[] =
    "local uid,profile=...; local parent=aha.GetIDByAIN(uid); "
    "assert(parent,'parent not present'); parent=tonumber(parent); "
    "local types={dimmable_light=265,color_temperature_light=278,cover=281}; "
    "local devices=assert(aha.GetDeviceList(),'device list unavailable'); "
    "local target=parent; "
    "if types[profile] then "
      "target=nil; for _,d in ipairs(devices) do "
        "local id=tonumber(d.ID); local u=aha.GetETSIUnit(id); "
        "if type(u)=='table' and tonumber(u.ETSIDEVICEID)==parent and tonumber(u.UNITID)==1 "
          "and tonumber(u.UNITTYPE)==types[profile] then target=id; break end "
      "end; assert(target,'native unit not ready'); "
      "assert(type((aha.GetLevelControl(target)))=='table','level interface not ready'); "
      "if profile~='cover' then assert(type((aha.GetSimpleOnOff(target)))=='table','on/off interface not ready') end "
    "end; "
    "local identifier; for _,d in ipairs(devices) do "
      "if tonumber(d.ID)==target then identifier=d.Identifyer; break end end; "
    "assert(type(identifier)=='string' and #identifier>0,'native identifier unavailable'); "
    /* Native SetAVMButtonScreenConfig rebuilds the other widgets on the same
     * page using their numeric children IDs. During startup those can still
     * be zero despite a valid saved identifier; a write would erase that AIN.
     * Features 9 (guest Wi-Fi), 10 (answering machine), 14 (routine) and
     * 15 (Wi-Fi) use no target or non-device target IDs, not device AINs. */
    "local function page_ready(widgets,screen) "
      "for _,w in ipairs(widgets) do if w.screen==screen then "
        "local f=w.featuretype; if f~=9 and f~=10 and f~=14 and f~=15 then "
          "assert(type(w.childrenidentifier)=='table' and type(w.childrenids)=='table' "
            "and #w.childrenidentifier>0 and #w.childrenidentifier==#w.childrenids,'page targets incomplete'); "
          "for i,ain in ipairs(w.childrenidentifier) do "
            "assert(type(ain)=='string' and #ain>0,'page target identifier empty'); "
            "local resolved=tonumber((aha.GetIDByAIN(ain))); "
            "assert(resolved and resolved>0 and resolved==tonumber(w.childrenids[i]),'page targets not resolved'); "
          "end "
        "end "
      "end end "
    "end; "
    "local pending={}; local widget_count=0; for _,d in ipairs(devices) do "
      "local id=tonumber(d.ID); if type((aha.GetAVMButton(id)))=='table' then "
        "local widgets=assert(aha.GetAVMButtonScreenConfig(id),'screen config unavailable'); "
        "widget_count=widget_count+#widgets; "
        "for _,w in ipairs(widgets) do "
          "local match=false; for _,child in ipairs(w.childrenidentifier or {}) do "
            "if child==identifier then match=true end end; "
          "if match then assert(w.screen and w.position and w.featuretype and w.name "
            "and type(w.childrenidentifier)=='table' and w.featureconfig~=nil,'incomplete widget'); "
            "page_ready(widgets,w.screen); pending[#pending+1]={id=id,widget=w} end "
        "end "
      "end "
    "end; assert(widget_count>0,'controller widgets not ready'); "
    "for _,p in ipairs(pending) do local w=p.widget; "
      "aha.SetAVMButtonScreenConfig(p.id,w.screen,w.position,w.featuretype,w.name,w.childrenidentifier,w.featureconfig) "
    "end; print('tentahacle-aha-control: submitted '..#pending..' existing widget(s) for '..uid)";

static int valid_profile(const char *profile){
    return !strcmp(profile,"switch")||!strcmp(profile,"dimmable_light")||
        !strcmp(profile,"color_temperature_light")||!strcmp(profile,"cover")||!strcmp(profile,"thermostat");
}
int main(int argc,char **argv){
    int resync=argc==4&&!strcmp(argv[1],"resync")&&valid_uid(argv[2])&&valid_profile(argv[3]);
    if(!resync&&(argc!=3||strcmp(argv[1],"delete")||!valid_uid(argv[2]))){
        fprintf(stderr,"usage: %s delete UID | resync UID PROFILE\n",argv[0]);return 2;
    }
    void *lua=dlopen("liblua.so.1",RTLD_LAZY|RTLD_GLOBAL);
    void *aha=lua?dlopen("libaha.so.1",RTLD_LAZY|RTLD_GLOBAL):NULL;
    if(!lua||!aha){fprintf(stderr,"tentahacle-aha-control: %s\n",dlerror());if(lua)dlclose(lua);return 3;}
    void *(*create)(void)=load_symbol(lua,"luaL_newstate");
    void (*openlibs)(void*)=load_symbol(lua,"luaL_openlibs");
    int (*openaha)(void*)=load_symbol(aha,"luaopen_libaha");
    int (*loadstring)(void*,const char*)=load_symbol(lua,"luaL_loadstring");
    int (*call)(void*,int,int,int)=load_symbol(lua,"lua_pcall");
    int (*toboolean)(void*,int)=load_symbol(lua,"lua_toboolean");
    const char *(*tostring)(void*,int,size_t*)=load_symbol(lua,"lua_tolstring");
    void (*settop)(void*,int)=load_symbol(lua,"lua_settop");
    void (*pushstring)(void*,const char*)=load_symbol(lua,"lua_pushstring");
    void (*close_lua)(void*)=load_symbol(lua,"lua_close");
    if(!create||!openlibs||!openaha||!loadstring||!call||!toboolean||!tostring||!settop||!pushstring||!close_lua){dlclose(aha);dlclose(lua);return 3;}
    void *state=create();if(!state){dlclose(aha);dlclose(lua);return 3;}
    openlibs(state);openaha(state);
    if(resync){
        int result=loadstring(state,resync_script);
        if(!result){pushstring(state,argv[2]);pushstring(state,argv[3]);result=call(state,2,0,0);}
        if(result){const char *message=tostring(state,-1,NULL);fprintf(stderr,"tentahacle-aha-control: resync pending: %s\n",message?message:"Lua error");}
        close_lua(state);dlclose(aha);dlclose(lua);return result?4:0;
    }
    char script[512];
    int length=snprintf(script,sizeof(script),
        "local i=aha.GetIDByAIN('%s');if i then aha.DeleteDevice(i) end",argv[2]);
    int result=length<=0||(size_t)length>=sizeof(script)||loadstring(state,script)||call(state,0,0,0);
    int absent=0;
    for(unsigned attempt=0;!result&&attempt<30;attempt++){
        length=snprintf(script,sizeof(script),
            "return aha.GetIDByAIN('%s')==nil",argv[2]);
        result=length<=0||(size_t)length>=sizeof(script)||loadstring(state,script)||call(state,0,1,0);
        if(result)break;
        absent=toboolean(state,-1);settop(state,-2);
        if(absent)break;
        usleep(100000);
    }
    if(!result&&!absent){fprintf(stderr,"tentahacle-aha-control: device still present\n");result=1;}
    if(result){
        const char *message=tostring(state,-1,NULL);
        if(message)fprintf(stderr,"tentahacle-aha-control: Lua: %s\n",message);
    }
    close_lua(state);dlclose(aha);dlclose(lua);
    return result?4:0;
}
