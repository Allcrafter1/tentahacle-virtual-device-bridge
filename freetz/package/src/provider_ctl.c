// SPDX-License-Identifier: MIT OR Apache-2.0
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc,char **argv){
    if(argc<2||argc>3){fprintf(stderr,"usage: %s COMMAND [SOCKET]\n",argv[0]);return 2;}
    const char *path=argc==3?argv[2]:"/var/tmp/aha-virtual-provider.ctl";
    if(strlen(path)>=sizeof(((struct sockaddr_un*)0)->sun_path))return 2;
    int socket_fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(socket_fd<0)return 3;
    struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
    struct timeval timeout={3,0};
    /* A full/stale local accept queue can otherwise leave connect() blocked
     * forever.  The watchdog must always regain control so it can restart the
     * isolated AHA process. */
    if(setsockopt(socket_fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) ||
       setsockopt(socket_fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))){close(socket_fd);return 4;}
    if(connect(socket_fd,(struct sockaddr*)&address,sizeof(address))){close(socket_fd);return 4;}
    char command[256];int command_length=snprintf(command,sizeof(command),"%s\n",argv[1]);
    if(command_length<=0||command_length>=(int)sizeof(command)){close(socket_fd);return 5;}
    ssize_t sent;
    do {sent=send(socket_fd,command,(size_t)command_length,MSG_NOSIGNAL);}while(sent<0&&errno==EINTR);
    if(sent!=command_length){close(socket_fd);return 5;}
    char reply[2048];ssize_t length;int result=7;
    for(;;){
        do {length=recv(socket_fd,reply,sizeof(reply),MSG_TRUNC);}while(length<0&&errno==EINTR);
        if(length==0)break;
        if(length<0||(size_t)length>sizeof(reply)){result=7;break;}
        size_t offset=0;
        while(offset<(size_t)length){
            ssize_t written=write(STDOUT_FILENO,reply+offset,(size_t)length-offset);
            if(written<0&&errno==EINTR)continue;
            if(written<=0){close(socket_fd);return 6;}
            offset+=(size_t)written;
        }
        result=0;
        if(strcmp(argv[1],"WATCH"))break;
    }
    close(socket_fd);return result;
}
