// SPDX-License-Identifier: MIT OR Apache-2.0
/* Exercise the real command dispatcher with exact-sized input allocations. */
#include "../freetz/package/src/virtual_provider.c"
#include <assert.h>
static uint32_t seed=0x7530440;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static ssize_t passthrough(int fd,const void *p,size_t n,int flags){
    (void)fd;(void)p;(void)flags;return (ssize_t)n;
}
int main(void){
    enabled=1;legacy_enabled=1;incoming_fd=42;encoder.fd=-1;generation=1;
    original_send=passthrough;
    tvb_registry_init(&registry);
    for(unsigned profile=0;profile<TVB_PROFILE_COUNT;profile++){
        char uid[20];snprintf(uid,sizeof(uid),"TVB%016X",profile+1);
        assert(tvb_registry_add(&registry,registry.revision,uid,"Fuzz",(tvb_device_profile)profile,NULL)==TVB_REGISTRY_OK);
        device_values[profile]=legacy_state;unit_generation[profile]=generation;
    }
    const unsigned functions[]={15,25,55,57,98,110,117,118};
    const unsigned inner_functions[]={15,108,110,125};
    for(unsigned run=0;run<100000;run++){
        size_t n=run<513?run:random_word()%513;
        if(run%3==0)n=40;
        if(run%3==1)n=44;
        unsigned char *p=malloc(n?n:1);assert(p);
        for(size_t i=0;i<n;i++)p[i]=(unsigned char)random_word();
        if(n>=24){
            p[0]=7;p[1]=3;write_be16(p+2,(unsigned)n);
            write_be16(p+8,450+random_word()%12);
            write_be32(p+12,(uint32_t)n-16);
            write_be32(p+16,functions[random_word()%8]);write_be16(p+20,(unsigned)n-24);
        }
        if(n>=36&&run%2){
            write_be32(p+16,118);write_be16(p+24,1);
            write_be32(p+28,inner_functions[random_word()%4]);write_be16(p+32,(unsigned)n-36);
        }
        if(n>=44&&run%5==0)write_be32(p+40,random_word()%8);
        unsigned char before[512];memcpy(before,p,n);
        assert(send_frame(run%11==0?43:42,p,n,0)==(ssize_t)n);
        assert(!memcmp(before,p,n));
        free(p);
        struct command_event event;
        while(provider_event_pop(&event_queue,&event)){
            char output[224];
            assert(memchr(event.endpoint,0,sizeof(event.endpoint)));
            assert(provider_event_format(&event,output,sizeof(output))>0);
        }
    }
    const char *commands[]={
        "ADD TVB0123456789ABCDEF switch Name",
        "RESTORE TVB0000000000000002 457 dimmable_light Name",
        "RENAME TVB0123456789ABCDEF Name",
        "SET TVB0123456789ABCDEF 1",
        "LEVEL TVB0123456789ABCDEF 40",
        "REMOVE TVB0123456789ABCDEF", "GET"
    };
    for(unsigned run=0;run<50000;run++){
        tvb_registry_init(&registry);
        assert(tvb_registry_add(&registry,0,"TVB0123456789ABCDEF","Name",
            (tvb_device_profile)(run%TVB_PROFILE_COUNT),NULL)==TVB_REGISTRY_OK);
        const char *base=commands[run%7];size_t n=strlen(base);
        if(run%3==0)n=random_word()%(n+1);
        char *text=malloc(n+1);assert(text);memcpy(text,base,n);text[n]=0;
        if(n&&run%3==1)text[random_word()%n]=(char)(1+random_word()%255);
        (void)dynamic_control(text,n+1);unselect_device();
        assert(registry.count<=TVB_REGISTRY_CAPACITY);
        for(size_t i=0;i<registry.count;i++){
            assert(tvb_registry_valid_uid(registry.devices[i].uid));
            assert(tvb_registry_valid_name(registry.devices[i].name));
        }
        free(text);
    }
    enabled=0;
    puts("PASS 100000 dispatcher + 50000 control-parser cases: legacy/dynamic, malformed lengths, immutable input, bounded events");
    return 0;
}
