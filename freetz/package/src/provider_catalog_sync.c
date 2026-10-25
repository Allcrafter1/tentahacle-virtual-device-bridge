// SPDX-License-Identifier: MIT OR Apache-2.0
#include "provider_catalog_sync.h"
#include <string.h>
static int valid(const unsigned char*p,size_t n){
 return p&&n==12&&p[0]==5&&p[1]<=3&&p[2]==0&&p[3]==12&&
 (p[4]||p[5]||p[6]||p[7])&&!p[8]&&!p[9]&&!p[10]&&!p[11];
}
static uint64_t next(struct provider_catalog_sync*s){
 if(s->serial==UINT64_MAX){s->overflow=1;return 0;}return ++s->serial;
}
int provider_catalog_capture(struct provider_catalog_sync*s,int fd,int peer,const void*v,size_t n){
 if(!s||fd<0||peer<0||fd==peer||!valid(v,n))return 0;
 struct provider_catalog_client*c=0;
 for(unsigned i=0;i<TVB_CATALOG_CLIENTS;i++)if(s->clients[i].used&&s->clients[i].fd==fd){c=&s->clients[i];break;}
 if(c&&c->peer==peer&&!memcmp(c->frame,v,12))return 1;
 if(!c)for(unsigned i=0;i<TVB_CATALOG_CLIENTS;i++)if(!s->clients[i].used){c=&s->clients[i];break;}
 if(!c){s->overflow=1;return 0;}
 uint64_t generation=next(s);if(!generation)return 0;
 c->used=1;c->fd=fd;c->peer=peer;c->generation=generation;memcpy(c->frame,v,12);return 1;
}
void provider_catalog_forget_fd(struct provider_catalog_sync*s,int fd){
 if(!s)return;
 for(unsigned i=0;i<TVB_CATALOG_CLIENTS;i++)if(s->clients[i].used&&(s->clients[i].fd==fd||s->clients[i].peer==fd))memset(&s->clients[i],0,sizeof(s->clients[i]));
 /* overflow deliberately stays fail-closed until the whole state is reset. */
}
void provider_catalog_note_native_write(struct provider_catalog_sync*s,int fd,size_t requested,ssize_t result){
 if(!s)return;
 for(unsigned i=0;i<TVB_CATALOG_CLIENTS;i++){
  struct provider_catalog_client*c=&s->clients[i];
  if(!c->used||c->fd!=fd)continue;
  if(result>0&&(size_t)result<requested)c->native_partial=1;
  else if(result>0&&(size_t)result==requested)c->native_partial=0;
  return;
 }
}
void provider_catalog_request(struct provider_catalog_sync*s){if(s)s->pending=1;}
int provider_catalog_take(struct provider_catalog_sync*s,struct provider_catalog_ticket*t){
 if(!s||!t)return -1;
 if(!s->pending||s->inflight)return 0;
 if(s->overflow)return -1;
 struct provider_catalog_client*c=0;
 for(unsigned i=0;i<TVB_CATALOG_CLIENTS;i++)if(s->clients[i].used){if(c)return -1;c=&s->clients[i];}
 if(!c||c->native_partial)return -1;
 uint64_t token=next(s);if(!token)return -1;
 t->fd=c->fd;t->peer=c->peer;t->generation=c->generation;t->token=token;memcpy(t->frame,c->frame,12);
 s->active_token=token;s->inflight=1;s->pending=0;return 1;
}
int provider_catalog_finish(struct provider_catalog_sync*s,const struct provider_catalog_ticket*t,int success){
 if(!s||!t||!s->inflight||s->active_token!=t->token)return 0;
 int still_valid=0;
 for(unsigned i=0;i<TVB_CATALOG_CLIENTS;i++){const struct provider_catalog_client*c=&s->clients[i];
  if(c->used&&c->fd==t->fd&&c->peer==t->peer&&c->generation==t->generation&&!memcmp(c->frame,t->frame,12))still_valid=1;
 }
 s->inflight=0;s->active_token=0;if(!success||!still_valid)s->pending=1;return 1;
}
