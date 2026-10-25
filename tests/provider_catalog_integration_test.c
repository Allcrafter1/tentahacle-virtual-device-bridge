// SPDX-License-Identifier: MIT OR Apache-2.0
/* Runs against the real provider via LD_PRELOAD; no AVM daemon involved. */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>

#define UID "TVB00000000000000C1"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"catalog integration line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned be16(const unsigned char *p){return (unsigned)p[0]*256+p[1];}
static unsigned be32(const unsigned char *p){return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];}
static int quiet(int fd){struct pollfd p={fd,POLLIN,0};return poll(&p,1,150)==0;}
static void pair(int p[2]){
 CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,p)==0);
 struct timeval t={2,0};
 CHECK(!setsockopt(p[0],SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t)));
 CHECK(!setsockopt(p[1],SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t)));
}
static unsigned frame(int fd,unsigned char *p,unsigned fn){
 CHECK(recv(fd,p,4,MSG_WAITALL)==4);
 unsigned n=be16(p+2);CHECK(n>=16&&n<=512);
 CHECK(recv(fd,p+4,n-4,MSG_WAITALL)==(ssize_t)n-4);
 if(!(fn?(p[0]==7&&be32(p+16)==fn):p[0]==4))
  fprintf(stderr,"expected fn=%u, received type=%u fn=%u len=%u\n",fn,p[0],be32(p+16),n);
 CHECK(fn?(p[0]==7&&be32(p+16)==fn):p[0]==4);
 return n;
}
static void command(const char *s){
 const char *path=getenv("AHA_VIRTUAL_CONTROL_PATH");CHECK(path);
 struct sockaddr_un a={.sun_family=AF_UNIX};CHECK(strlen(path)<sizeof(a.sun_path));strcpy(a.sun_path,path);
 int fd=socket(AF_UNIX,SOCK_SEQPACKET,0);CHECK(fd>=0);
 CHECK(!connect(fd,(struct sockaddr *)&a,sizeof(a)));
 CHECK(send(fd,s,strlen(s),0)==(ssize_t)strlen(s));
 char response[2048]={0};CHECK(recv(fd,response,sizeof(response)-1,0)>0);
 CHECK(strstr(response,"\"ok\":true"));CHECK(!close(fd));
}
static void unit(int server,unsigned char ack[216]){
 unsigned char p[512];command("UNIT " UID);
 CHECK(frame(server,p,98)==416);
 memcpy(ack,p,16);ack[2]=0;ack[3]=216;
 ack[12]=ack[13]=ack[14]=0;ack[15]=200;
 memcpy(ack+16,p+216,200); /* Actual BE variant, including revision. */
 CHECK(frame(server,p,118)==40);CHECK(frame(server,p,118)==44);
}
typedef int (*decode_fn)(void *,void *,unsigned,void *,unsigned,unsigned);
static decode_fn decoder;
static void decode(const unsigned char *ack,int result,unsigned capacity,int inplace,int null_output){
 unsigned char input[216],original[216],output[256],expected[256];
 memcpy(input,ack,216);memcpy(original,ack,216);
 memset(output,0xa5,sizeof(output));memcpy(expected,output,sizeof(output));
 unsigned n=capacity<216?capacity:216;
 for(unsigned i=0;i<n;i++)expected[i]=ack[i]^0x5a;
 errno=EDOM;
 CHECK(decoder(&result,input,216,null_output?NULL:(inplace?input:output),0x22038,capacity)==result);
 CHECK(errno==ERANGE);
 if(!null_output&&inplace){
  CHECK(!memcmp(input,expected,n));CHECK(!memcmp(input+n,original+n,216-n));
 }else CHECK(!memcmp(input,original,216));
 if(!null_output&&!inplace)CHECK(!memcmp(output,expected,sizeof(output)));
 else {unsigned char untouched[256];memset(untouched,0xa5,sizeof(untouched));CHECK(!memcmp(output,untouched,sizeof(output)));}
}
static void acknowledge(int server,const unsigned char *ack){(void)server;decode(ack,216,256,0,0);}
static void request(int client,const unsigned char expected[12]){
 unsigned char p[12];CHECK(recv(client,p,sizeof(p),MSG_WAITALL)==sizeof(p));
 CHECK(!memcmp(p,expected,sizeof(p)));CHECK(quiet(client));
}
int main(void){
 int c[2],s[2];unsigned char p[512],ack[216],bad[216];
 decoder=(decode_fn)dlsym(RTLD_DEFAULT,"convert_fromBigEndian_toMachine_ng");CHECK(decoder);
 const unsigned char config[12]={5,3,0,12,0x12,0x34,0x56,0x78,0,0,0,0};
 CHECK(!prctl(PR_SET_NAME,"cR/TX-test",0,0,0));pair(c);
 CHECK(write(c[0],config,12)==12);request(c[1],config);
 CHECK(!prctl(PR_SET_NAME,"sR/TX-test",0,0,0));pair(s);
 const unsigned char hello[24]={1,3,0,24,0,0,0,3,0,0,0,3,0,2,32,56};
 CHECK(write(s[0],hello,24)==24);CHECK(recv(s[1],p,24,MSG_WAITALL)==24);
 command("GET");command("GET");
 CHECK(quiet(s[1])); /* Empty startup completed before ADD. */
 command("ADD " UID " dimmable_light Catalog lamp");frame(s[1],p,0);frame(s[1],p,95);
 unit(s[1],ack);CHECK(quiet(c[1]));
 /* Real csock stream forwarding splits the 216-byte frame at 168 bytes.
  * Neither fragment is proof of successful native decoding. */
 CHECK(send(s[1],ack,168,0)==168);CHECK(send(s[1],ack+168,48,0)==48);
 CHECK(recv(s[0],p,216,MSG_WAITALL)==216);CHECK(!memcmp(p,ack,216));
 CHECK(quiet(c[1]));
 decode(ack,-1,256,0,0);CHECK(quiet(c[1]));
 decode(ack,215,256,0,0);CHECK(quiet(c[1]));
 decode(ack,217,256,0,0);CHECK(quiet(c[1]));
 decode(ack,216,215,0,0);CHECK(quiet(c[1]));
 decode(ack,216,256,0,1);CHECK(quiet(c[1]));
 memcpy(bad,ack,216);bad[7]^=1;decode(bad,216,256,0,0);CHECK(quiet(c[1]));
 /* A write succeeding is not acceptance. Every identity/revision mismatch
  * below must leave the captured native client untouched. */
 const unsigned offsets[]={156,24,112,152};
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
  memcpy(bad,ack,sizeof(bad));bad[offsets[i]]^=1;
  acknowledge(s[1],bad);CHECK(quiet(c[1]));
 }
 acknowledge(s[1],ack);request(c[1],config);
 acknowledge(s[1],ack);CHECK(quiet(c[1]));
 unsigned char previous[216];memcpy(previous,ack,216);
 unit(s[1],ack);CHECK(memcmp(previous+156,ack+156,8));
 acknowledge(s[1],ack);CHECK(quiet(c[1]));
 command("RENAME " UID " Catalog renamed");frame(s[1],p,0);frame(s[1],p,95);
 unit(s[1],ack);decode(ack,216,216,1,0);request(c[1],config);
 /* Reuse the captured descriptor for an unrelated socket: no stale write. */
 int reused=c[0];CHECK(!close(c[0]));CHECK(!close(c[1]));
 CHECK(!prctl(PR_SET_NAME,"foreign-test",0,0,0));int foreign[2];pair(foreign);
 CHECK(foreign[0]==reused); /* Linux allocates the lowest free descriptor. */
 command("RENAME " UID " Catalog after close");frame(s[1],p,0);frame(s[1],p,95);
 unit(s[1],ack);acknowledge(s[1],ack);CHECK(quiet(foreign[1]));
 CHECK(!close(foreign[0]));CHECK(!close(foreign[1]));CHECK(!close(s[0]));CHECK(!close(s[1]));
 puts("PASS catalog integration: fragmented transport, converter guards and transparency, matched acceptance, deduplication, rename, fd reuse");
 return 0;
}
