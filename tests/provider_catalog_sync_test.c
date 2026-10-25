// SPDX-License-Identifier: MIT OR Apache-2.0
#include "provider_catalog_sync.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 struct provider_catalog_sync s={0};struct provider_catalog_ticket t,old;
 unsigned char frame[12]={5,3,0,12,0,0,0,7,0,0,0,0},copy[12];memcpy(copy,frame,12);
 provider_catalog_request(&s);assert(provider_catalog_take(&s,&t)==-1&&s.pending);
 assert(!provider_catalog_capture(&s,3,3,frame,12));assert(!provider_catalog_capture(&s,3,4,frame,11));frame[8]=1;assert(!provider_catalog_capture(&s,3,4,frame,12));frame[8]=0;
 assert(provider_catalog_capture(&s,3,4,frame,12));uint64_t g=s.clients[0].generation;
 assert(provider_catalog_capture(&s,3,4,frame,12)&&s.clients[0].generation==g);
 provider_catalog_request(&s);provider_catalog_request(&s);assert(provider_catalog_take(&s,&t)==1);assert(t.fd==3&&t.peer==4&&!memcmp(t.frame,frame,12));assert(!provider_catalog_take(&s,&old));
 assert(provider_catalog_finish(&s,&t,1));assert(!provider_catalog_take(&s,&t));assert(!provider_catalog_finish(&s,&t,1));
 /* Failed transport retains work but has no autonomous retry mechanism. */
 provider_catalog_request(&s);assert(provider_catalog_take(&s,&t)==1);assert(provider_catalog_finish(&s,&t,0)&&s.pending);assert(provider_catalog_take(&s,&t)==1);
 provider_catalog_request(&s);assert(provider_catalog_finish(&s,&t,1)&&s.pending);assert(provider_catalog_take(&s,&t)==1);assert(provider_catalog_finish(&s,&t,1)&&!s.pending);
 /* Two clients suppress sends; closing either endpoint resolves ambiguity. */
 assert(provider_catalog_capture(&s,5,6,frame,12));provider_catalog_request(&s);assert(provider_catalog_take(&s,&t)==-1);provider_catalog_forget_fd(&s,6);assert(provider_catalog_take(&s,&t)==1);
 old=t;provider_catalog_forget_fd(&s,3);assert(provider_catalog_capture(&s,3,8,frame,12));assert(provider_catalog_finish(&s,&old,1)&&s.pending);assert(provider_catalog_take(&s,&t)==1&&t.generation!=old.generation);assert(!provider_catalog_finish(&s,&old,1));assert(provider_catalog_finish(&s,&t,1));
 /* Same fd with changed connection handle invalidates old generation. */
 provider_catalog_request(&s);assert(provider_catalog_take(&s,&old)==1);frame[7]=9;assert(provider_catalog_capture(&s,3,8,frame,12));assert(provider_catalog_finish(&s,&old,1)&&s.pending);assert(provider_catalog_take(&s,&t)==1&&t.frame[7]==9);assert(provider_catalog_finish(&s,&t,1));
 assert(!memcmp(copy,(unsigned char[]){5,3,0,12,0,0,0,7,0,0,0,0},12));
 /* Native partial write cannot be interleaved with catalog requests. */
 provider_catalog_request(&s);provider_catalog_note_native_write(&s,3,100,40);assert(provider_catalog_take(&s,&t)==-1&&s.pending);
 provider_catalog_note_native_write(&s,3,60,-1);assert(provider_catalog_take(&s,&t)==-1);
 provider_catalog_note_native_write(&s,3,0,0);assert(provider_catalog_take(&s,&t)==-1);
 provider_catalog_note_native_write(&s,3,60,0);assert(provider_catalog_take(&s,&t)==-1);
 provider_catalog_note_native_write(&s,999,60,60);assert(provider_catalog_take(&s,&t)==-1);
 provider_catalog_note_native_write(&s,3,60,60);assert(provider_catalog_take(&s,&t)==1);assert(provider_catalog_finish(&s,&t,1));
 provider_catalog_request(&s);provider_catalog_note_native_write(&s,3,100,20);provider_catalog_forget_fd(&s,8);assert(provider_catalog_take(&s,&t)==-1);assert(provider_catalog_capture(&s,3,8,frame,12));assert(provider_catalog_take(&s,&t)==1);assert(provider_catalog_finish(&s,&t,1));
 puts("PASS native partial/error/completion/close guard; capture/validation, coalescing, failure recovery, ambiguity recovery, close/reuse, changed handle, stale ticket, in-flight new work");return 0;
}
