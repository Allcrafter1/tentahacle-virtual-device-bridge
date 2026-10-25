// SPDX-License-Identifier: MIT OR Apache-2.0
/* Deterministic malformed-input sweep; exact allocations expose overreads
 * under ASan. No sockets, router access or unbounded random input. */
#include "control_parse.h"
#include "provider_codec.h"
#include "registry_store.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t rng=0x4407530;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
    tvb_device_registry registry, before;
    tvb_registry_init(&before);
    assert(tvb_registry_add(&before,0,"TVB0123456789ABCDEF","Kitchen",TVB_PROFILE_DIMMABLE_LIGHT,NULL)==TVB_REGISTRY_OK);
    char *seed=tvb_registry_serialize(&before);assert(seed);
    size_t seed_size=strlen(seed);
    for(unsigned test=0;test<30000;test++){
        size_t n=test<513?test:next()%513;
        unsigned char *bytes=malloc(n?n:1);assert(bytes);
        for(size_t i=0;i<n;i++)bytes[i]=(unsigned char)next();
        if(n>=4&&test%3==0){bytes[1]=3;write_be16(bytes+2,(unsigned)(next()%600));}
        (void)provider_complete_batch(bytes,n);
        free(bytes);
        size_t len=test%2?n:seed_size;
        char *text=malloc(len+1);assert(text);
        if(test%2)for(size_t i=0;i<len;i++)text[i]=(char)(1+next()%255);
        else {
            memcpy(text,seed,len);
            for(unsigned edit=0;edit<1+test%5;edit++)text[next()%len]=(char)(1+next()%127);
        }
        text[len]=0;
        uint32_t stamp;unsigned fields[4];
        (void)tvb_parse_timestamp(text,&stamp);
        (void)tvb_parse_schedule(text,fields);
        (void)tvb_registry_valid_uid(text);
        (void)tvb_registry_valid_name(text);
        registry=before;char error[128];
        if(!tvb_registry_deserialize(text,&registry,error,sizeof(error)))
            assert(!memcmp(&registry,&before,sizeof(registry)));
        else {
            char *out=tvb_registry_serialize(&registry);assert(out);
            tvb_device_registry roundtrip;
            assert(tvb_registry_deserialize(out,&roundtrip,error,sizeof(error)));
            assert(!memcmp(&registry,&roundtrip,sizeof(registry)));
            tvb_registry_serialized_free(out);
        }
        free(text);
    }
    tvb_registry_serialized_free(seed);
    puts("PASS 30000 bounded malformed frame/text/registry cases; failed parse preserves state");
    return 0;
}
