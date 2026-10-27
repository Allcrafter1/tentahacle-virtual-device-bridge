// SPDX-License-Identifier: MIT OR Apache-2.0
/* Exercise the actual private dispatcher and locked transport failure path.
 * No AHA daemon, preload environment, or network connection is needed. */
#include "../freetz/package/src/virtual_provider.c"
#include <assert.h>
#include <sys/wait.h>
static unsigned forwarded, writes;
static int blocked_ready[2], blocked_release[2], blocked_observed_fd=-1;
struct blocked_write_result {
    int fd;
    const unsigned char *frame;
    size_t size;
    ssize_t result;
};
static ssize_t forward(int fd,const void *p,size_t n,int flags) {
    (void)fd;(void)p;(void)flags;++forwarded;return (ssize_t)n;
}
static ssize_t partial_then_again(int fd,const void *p,size_t n,int flags) {
    assert(flags==(MSG_DONTWAIT|MSG_NOSIGNAL));
    if (!writes++) { assert(n>=2);return syscall(SYS_write,fd,p,2); }
    errno=EAGAIN;return -1;
}
static ssize_t blocked_native_write(int fd,const void *p,size_t n) {
    (void)p;blocked_observed_fd=fd;
    char byte=1;
    assert(syscall(SYS_write,blocked_ready[1],&byte,1)==1);
    while(syscall(SYS_read,blocked_release[0],&byte,1)<0)assert(errno==EINTR);
    return (ssize_t)n;
}
static void *run_blocked_native_write(void *opaque) {
    struct blocked_write_result *result=opaque;
    assert(!prctl(PR_SET_NAME,"cR/TX-test",0,0,0));
    result->result=write(result->fd,result->frame,result->size);
    return NULL;
}
static void frame(unsigned char *p,unsigned n,unsigned remote,unsigned function) {
    memset(p,0,n);p[0]=7;p[1]=3;write_be16(p+2,n);write_be16(p+8,remote);
    write_be32(p+12,n-16);write_be32(p+16,function);write_be16(p+20,n-24);
}
static void control(const char *command) {
    int p[2],watchers[4]={-1,-1,-1,-1};
    assert(original_socketpair(AF_UNIX,SOCK_SEQPACKET,0,p)==0);
    ssize_t sent=original_send(p[1],command,strlen(command),0);
    if(sent!=(ssize_t)strlen(command))fprintf(stderr,"control send failed: result=%zd errno=%d\n",sent,errno);
    assert(sent==(ssize_t)strlen(command));
    handle_control_client(p[0],watchers);
    char reply[2048];assert(recv(p[1],reply,sizeof(reply),MSG_DONTWAIT)>0);
    original_close(p[1]);
}
int main(void) {
    ssize_t (*saved_send)(int,const void *,size_t,int)=original_send;
    enabled=1;legacy_enabled=1;incoming_fd=42;encoder.fd=-1;original_send=forward;
    unsigned char p[44];struct command_event ev;
    for(unsigned remote=451;remote<=453;++remote) {
        unsigned n=remote==451?32:44,body=remote==451?24:36;
        frame(p,n,remote,remote==451?110:118);
        if(remote!=451){write_be16(p+24,1);write_be32(p+28,110);write_be16(p+32,8);}
        p[body]=20;write_be32(p+body+4,99);
        assert(send_frame(42,p,n,0)==n);
        assert(!provider_event_pop(&event_queue,&ev));assert(command_count==0);
    }
    assert(forwarded==3);
    frame(p,40,455,117);write_be32(p+24,0);write_be32(p+28,120);write_be32(p+32,UINT32_MAX-59);
    assert(send_frame(42,p,40,0)==40);assert(forwarded==4);
    assert(!provider_event_pop(&event_queue,&ev)&&command_count==0);
    write_be32(p+32,UINT32_MAX-120);
    assert(send_frame(42,p,40,0)==40);assert(provider_event_pop(&event_queue,&ev));
    assert(ev.thermostat_timer_end==UINT32_MAX&&ev.thermostat_timer_duration==120);
    /* Valid neighboring level types must still produce exactly one event. */
    for(unsigned remote=451;remote<=453;++remote) {
        unsigned n=remote==451?32:44,body=remote==451?24:36;
        frame(p,n,remote,remote==451?110:118);
        if(remote!=451){write_be16(p+24,1);write_be32(p+28,110);write_be16(p+32,8);}
        p[body]=20;write_be32(p+body+4,1);
        assert(send_frame(42,p,n,0)==n);
        assert(provider_event_pop(&event_queue,&ev)&&ev.has_level&&ev.level==20);
        assert(!provider_event_pop(&event_queue,&ev));
    }
    original_send=saved_send;
    encoder.state->thermostat_target=253;encoder.state->thermostat_heat_target=40;
    control("TARGET VIRT000000000006 22.0");
    assert(encoder.state->thermostat_target==253&&encoder.state->thermostat_heat_target==44);
    control("SCHEDULE VIRT000000000006 active 40 34 123 456");
    assert(encoder.state->thermostat_target==253&&encoder.state->thermostat_heat_target==44);
    assert(encoder.state->thermostat_comfort==40&&encoder.state->thermostat_reduced==34);
    control("MODE VIRT000000000006 heat");assert(encoder.state->thermostat_target==44);
    control("SCHEDULE VIRT000000000006 active 42 36 123 456");
    assert(encoder.state->thermostat_target==44&&encoder.state->thermostat_heat_target==44);
    /* A prefix followed by EAGAIN poisons the stream. Retire it without
     * closing the descriptor owned by AHA or losing the original errno. */
    int pair[2];assert(original_socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    encoder.fd=pair[0];incoming_fd=pair[1];announced=7;
    unsigned char request[12]={5,3,0,12,0,0,0,7};
    assert(provider_catalog_capture(&catalog_sync,pair[0],pair[1],request,12));
    original_send=partial_then_again;
    pthread_mutex_lock(&lock);
    writes=1; /* EAGAIN before any bytes is safely retryable. */
    assert(write_provider_frame(pair[0],request,12)==-1&&errno==EAGAIN);
    assert(encoder.fd==pair[0]&&incoming_fd==pair[1]&&catalog_sync.clients[0].used);
    struct provider_catalog_ticket ticket;
    provider_catalog_request(&catalog_sync);
    assert(provider_catalog_take(&catalog_sync,&ticket)==1);
    writes=0;
    assert(write_provider_frame(pair[0],request,12)==-1&&errno==EAGAIN);
    assert(encoder.fd==-1&&incoming_fd==-1&&announced==0);
    assert(!catalog_sync.clients[0].used);
    assert(provider_catalog_finish(&catalog_sync,&ticket,0)&&catalog_sync.pending);
    assert(provider_catalog_take(&catalog_sync,&ticket)==-1);
    pthread_mutex_unlock(&lock);
    assert(fcntl(pair[0],F_GETFD)>=0); /* shutdown, never close/reuse */
    unsigned char got[12];assert(recv(pair[1],got,12,MSG_DONTWAIT)==2);
    assert(!memcmp(got,request,2));assert(recv(pair[1],got,12,MSG_DONTWAIT)==0);
    original_send=saved_send;
    original_close(pair[0]);original_close(pair[1]);
    /* A fresh native connection can fulfill the retained catalog request. */
    assert(original_socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    pthread_mutex_lock(&lock);
    assert(provider_catalog_capture(&catalog_sync,pair[0],pair[1],request,12));
    assert(provider_catalog_take(&catalog_sync,&ticket)==1);
    assert(write_provider_frame(ticket.fd,ticket.frame,12)==12);
    assert(provider_catalog_finish(&catalog_sync,&ticket,1)&&!catalog_sync.pending);
    pthread_mutex_unlock(&lock);
    assert(recv(pair[1],got,12,MSG_DONTWAIT)==12&&!memcmp(got,request,12));
    original_close(pair[0]);original_close(pair[1]);enabled=0;

    /* A full native stream used to block write_provider_frame indefinitely
     * while it held the global lock, starving every control GET. Keep the fd
     * blocking to reproduce that old behavior, but fill it with MSG_DONTWAIT.
     * Run the assertion in a bounded child so a regression fails instead of
     * hanging the test job itself. */
    assert(original_socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    unsigned char fill[4096]={0};
    while(original_send(pair[0],fill,sizeof(fill),MSG_DONTWAIT|MSG_NOSIGNAL)>0){}
    assert(errno==EAGAIN||errno==EWOULDBLOCK);
    pid_t child=fork();assert(child>=0);
    if(!child){
        alarm(2);
        pthread_mutex_lock(&lock);
        errno=0;
        ssize_t result=write_provider_frame(pair[0],request,sizeof(request));
        int saved_errno=errno;
        pthread_mutex_unlock(&lock);
        if(result!=-1||(saved_errno!=EAGAIN&&saved_errno!=EWOULDBLOCK))_exit(2);
        if(pthread_mutex_trylock(&lock))_exit(3);
        pthread_mutex_unlock(&lock);
        _exit(0);
    }
    int child_status=0;
    assert(waitpid(child,&child_status,0)==child);
    assert(WIFEXITED(child_status)&&WEXITSTATUS(child_status)==0);
    original_close(pair[0]);original_close(pair[1]);

    /* Pairing and device firmware traffic can block a native cR/TX write for
     * many seconds. It must not hold the provider state/control mutex: GET
     * remains available and the watchdog must not kill an otherwise healthy
     * AHA process. Catalog injection must nevertheless stay serialized behind
     * the native frame. */
    assert(original_socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    assert(pipe(blocked_ready)==0&&pipe(blocked_release)==0);
    memset(&catalog_sync,0,sizeof(catalog_sync));
    pthread_mutex_lock(&lock);
    enabled=1;
    pair_count=1;pair_epoch=40;
    pairs[0].endpoints[0]=pair[0];pairs[0].endpoints[1]=pair[1];
    pairs[0].epoch=++pair_epoch;
    assert(provider_catalog_capture(&catalog_sync,pair[0],pair[1],request,sizeof(request)));
    provider_catalog_request(&catalog_sync);catalog_event=1;
    pthread_mutex_unlock(&lock);
    ssize_t (*saved_write)(int,const void *,size_t)=original_write;
    original_write=blocked_native_write;
    struct blocked_write_result blocked={pair[0],request,sizeof(request),-1};
    pthread_t writer;assert(!pthread_create(&writer,NULL,run_blocked_native_write,&blocked));
    unsigned char ready;
    while(syscall(SYS_read,blocked_ready[0],&ready,1)<0)assert(errno==EINTR);
    struct blocked_write_result blocked_second={pair[0],request,sizeof(request),-1};
    pthread_t second_writer;
    assert(!pthread_create(&second_writer,NULL,run_blocked_native_write,&blocked_second));
    struct pollfd second_ready={blocked_ready[0],POLLIN,0};
    assert(poll(&second_ready,1,100)==0); /* Native writes remain serialized. */
    assert(pthread_mutex_trylock(&lock)==0);
    assert(native_writes_inflight==1);
    process_catalog_event_locked();
    assert(catalog_event&&catalog_sync.pending&&!catalog_sync.inflight);
    pthread_mutex_unlock(&lock);
    control("GET");
    assert(syscall(SYS_write,blocked_release[1],&ready,1)==1);
    assert(!pthread_join(writer,NULL)&&blocked.result==(ssize_t)sizeof(request));
    while(syscall(SYS_read,blocked_ready[0],&ready,1)<0)assert(errno==EINTR);
    pthread_mutex_lock(&lock);
    assert(native_writes_inflight==1);
    process_catalog_event_locked();
    assert(catalog_event&&catalog_sync.pending&&!catalog_sync.inflight);
    pthread_mutex_unlock(&lock);
    control("GET");
    assert(syscall(SYS_write,blocked_release[1],&ready,1)==1);
    assert(!pthread_join(second_writer,NULL)&&
        blocked_second.result==(ssize_t)sizeof(request));
    assert(blocked_observed_fd>=0&&blocked_observed_fd!=pair[0]);
    pthread_mutex_lock(&lock);
    assert(!native_writes_inflight);
    process_catalog_event_locked();
    assert(!catalog_event&&!catalog_sync.pending&&!catalog_sync.inflight);
    pthread_mutex_unlock(&lock);
    assert(recv(pair[1],got,sizeof(got),MSG_DONTWAIT)==(ssize_t)sizeof(request));
    assert(!memcmp(got,request,sizeof(request)));

    /* Simulate close/reuse assigning the same fd numbers a new epoch while a
     * second native write is outside the mutex. Its stale completion must not
     * repopulate catalog state for the replacement connection. */
    memset(&catalog_sync,0,sizeof(catalog_sync));catalog_event=0;
    blocked_observed_fd=-1;blocked.result=-1;
    assert(!pthread_create(&writer,NULL,run_blocked_native_write,&blocked));
    while(syscall(SYS_read,blocked_ready[0],&ready,1)<0)assert(errno==EINTR);
    pthread_mutex_lock(&lock);
    assert(native_writes_inflight==1);
    pairs[0].epoch=++pair_epoch;
    pthread_mutex_unlock(&lock);
    control("GET");
    assert(syscall(SYS_write,blocked_release[1],&ready,1)==1);
    assert(!pthread_join(writer,NULL)&&blocked.result==(ssize_t)sizeof(request));
    assert(blocked_observed_fd>=0&&blocked_observed_fd!=pair[0]);
    assert(!native_writes_inflight);
    for(unsigned i=0;i<TVB_CATALOG_CLIENTS;i++)assert(!catalog_sync.clients[i].used);
    original_write=saved_write;pair_count=0;enabled=0;
    for(unsigned i=0;i<2;i++){
        original_close(blocked_ready[i]);original_close(blocked_release[i]);
    }
    original_close(pair[0]);original_close(pair[1]);

    tvb_registry_init(&registry);encoder.fd=-1;
    char control_text[256]="ADD TVB0123456789ABCDEF switch Original";
    assert(dynamic_control(control_text,sizeof(control_text))==1);
    tvb_device_registry unchanged=registry;
    strcpy(control_text,"RENAME TVB0123456789ABCDEFX Changed");
    assert(dynamic_control(control_text,sizeof(control_text))==0);
    assert(!memcmp(&registry,&unchanged,sizeof(registry)));
    strcpy(control_text,"ADD TVB0000000000000002switch Bad");
    assert(dynamic_control(control_text,sizeof(control_text))==0);
    assert(!memcmp(&registry,&unchanged,sizeof(registry)));
    strcpy(control_text,"RENAME TVB0123456789ABCDEF Valid");
    assert(dynamic_control(control_text,sizeof(control_text))==1);
    assert(!strcmp(registry.devices[0].name,"Valid"));
    puts("PASS invalid/valid levels, timer overflow/boundary, off/heat schedule feedback, bounded native/provider backpressure, zero/prefix-write failures and socket ownership");
    return 0;
}
