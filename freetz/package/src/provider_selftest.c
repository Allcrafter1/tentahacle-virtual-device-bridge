// SPDX-License-Identifier: MIT OR Apache-2.0
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <poll.h>
#include <stdint.h>
static unsigned u16(const unsigned char*p){return ((unsigned)p[0]<<8)|p[1];}
static unsigned u32(const unsigned char*p){return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];}
static uint64_t u64(const unsigned char*p){return ((uint64_t)u32(p)<<32)|u32(p+4);}
static int control_command(const char *path,const char *command){
 int c=socket(AF_UNIX,SOCK_SEQPACKET,0);char response[256];
 struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,path);
 if(c<0||connect(c,(struct sockaddr*)&a,sizeof(a))||
    send(c,command,strlen(command),0)!=(ssize_t)strlen(command)||
    recv(c,response,sizeof(response),0)<=0){if(c>=0)close(c);return 0;}
 close(c);return 1;
}
/* Isolated dynamic registry integration tests. No AVM daemon is involved. */
static int expect_control(const char *path,const char *command,int expected){
 int fd=socket(AF_UNIX,SOCK_SEQPACKET,0);char response[2048]={0};
 struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,path);
 struct timeval timeout={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 int ok=fd>=0&&!connect(fd,(struct sockaddr*)&a,sizeof(a))&&
     send(fd,command,strlen(command),0)==(ssize_t)strlen(command);
 ssize_t n=ok?recv(fd,response,sizeof(response)-1,0):-1;
 if(fd>=0)close(fd);
 return n>0&&strstr(response,expected?"\"ok\":true":"\"ok\":false");
}
/* Full-frame fixtures were captured from v0.1.4 (99bd3f2), independently of
 * the refactored encoders. Mask only clock-derived fields, never wire layout,
 * identity, capabilities, values or padding. Dynamic Fn98 fixtures additionally
 * encode the intentional paired-payload transport extension. The synthetic
 * v0.1.4 actual temperature is normalized because current builds deliberately
 * start it as unknown and test HA-provided values separately. Schedule
 * feedback preserves the current off/heat state rather than applying its
 * scheduled temperature (dynamic packet 47 keeps target=253/off).
 * Set TVB_PROVIDER_GOLDEN to opt in. */
static FILE *golden_file;
static unsigned golden_frame;
static int verify_golden(const unsigned char *packet, unsigned length) {
 const char *path=getenv("TVB_PROVIDER_GOLDEN");
 if(!path)return 1;
 if(!golden_file){golden_file=fopen(path,"r");if(!golden_file)return 0;}
 unsigned char normalized[512];
 char expected[1026],actual[1025];
 memcpy(normalized,packet,length);
 if(packet[0]==7){
  unsigned function=u32(packet+16);
  /* The old fixture zeroed only the low word; its high word was already
   * zero. Normalize the full native uint64 revision; all other bytes remain
   * checked against the original fixture. Timestamp units are checked below. */
  if(function==95&&length==48)memset(normalized+40,0,8);
  if(function==98&&length==416){memset(normalized+156,0,8);memset(normalized+356,0,8);}
  if(function==23&&length==32){memset(normalized+24,0,4);normalized[27]=200;}
  if(function==57&&length==40)normalized[28]=40;
  if((function==57||function==117)&&length==40)memset(normalized+32,0,4);
 }
 for(unsigned i=0;i<length;i++)snprintf(actual+i*2,3,"%02x",normalized[i]);
 ++golden_frame;
 if(!fgets(expected,sizeof(expected),golden_file))return 0;
 expected[strcspn(expected,"\r\n")]=0;
 if(strcmp(expected,actual)){
  fprintf(stderr,"golden packet %u differs\n",golden_frame);
  return 0;
 }
 return 1;
}
static int frame(int fd,unsigned char *p,unsigned remote,unsigned function){
 if(recv(fd,p,4,MSG_WAITALL)!=4)return 0;
 unsigned n=u16(p+2);if(n<16||n>512)return 0;
 if(recv(fd,p+4,n-4,MSG_WAITALL)!=(ssize_t)n-4)return 0;
 return verify_golden(p,n)&&u16(p+8)==remote&&(function? p[0]==7&&u32(p+16)==function:p[0]==4);
}
/* Both variants are one atomic outer frame, sharing identity and revision. */
static int paired_unit(const unsigned char *p){
 if(u16(p+2)!=416||u32(p+12)!=400||u32(p+16)!=98||u32(p+216)!=98||
    u16(p+20)!=192||u16(p+220)!=192||u64(p+156)!=u64(p+356))return 0;
 for(unsigned i=16;i<216;i++)
  if((i<112||i>=152)&&p[i]!=p[i+200])return 0;
 for(unsigned i=112;i<152;i+=4)
  for(unsigned j=0;j<4;j++)if(p[i+j]!=p[i+200+3-j])return 0;
 return u16(p+152)==u16(p+352);
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"dynamic test line %d: %s\n",__LINE__,#x);return 120;}}while(0)
static int dynamic_test(void){
 int s[2];unsigned char p[512]={1,3,0,24,0,0,0,3,0,0,0,3,0,2,32,56};
 const char *path=getenv("AHA_VIRTUAL_CONTROL_PATH");if(!path)path="/var/tmp/aha-virtual-provider.ctl";
 prctl(PR_SET_NAME,"sR/TX-test",0,0,0);
 /* Discovery slots must survive more than a lifetime total of 32 pairs.
  * Closing one end retires the pair even if the other fd is still open. */
 for (unsigned i=0;i<40;i++) {
  int retired[2];
  CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,retired));
  CHECK(!close(retired[0]));
  CHECK(!close(retired[1]));
 }
 CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,s));
 struct timeval timeout={2,0};setsockopt(s[0],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 setsockopt(s[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 CHECK(write(s[0],p,24)==24&&recv(s[1],p,24,MSG_WAITALL)==24);
 for(int i=0;i<100&&access(path,F_OK);i++)usleep(10000);
 int ready=0;for(int i=0;i<100&&!ready;i++){ready=expect_control(path,"GET",1);if(!ready)usleep(10000);}
 CHECK(ready);
 /* Closed WATCH clients must not fill all four slots when no events occur. */
 for(unsigned i=0;i<8;i++)CHECK(expect_control(path,"WATCH",1));
 /* A client may cancel after sending a request but before reading its reply.
  * The provider must not deliver SIGPIPE to its host AHA process. */
 int cancelled=socket(AF_UNIX,SOCK_SEQPACKET,0);
 struct sockaddr_un cancelled_address={.sun_family=AF_UNIX};
 strcpy(cancelled_address.sun_path,path);
 CHECK(cancelled>=0&&!connect(cancelled,(struct sockaddr*)&cancelled_address,sizeof(cancelled_address)));
 CHECK(send(cancelled,"GET",3,0)==3);
 CHECK(!close(cancelled));
 CHECK(expect_control(path,"GET",1));
 struct pollfd wait={s[1],POLLIN,0};CHECK(poll(&wait,1,50)==0);
 CHECK(expect_control(path,"SET 1",0));
 CHECK(expect_control(path,"ADD TVB0000000000000001 switch First",1));
 CHECK(frame(s[1],p,456,0)&&!strcmp((char*)p+20,"First")&&!strcmp((char*)p+112,"TVB0000000000000001"));
 CHECK(frame(s[1],p,456,35));CHECK(frame(s[1],p,456,15));
 CHECK(expect_control(path,"ADD TVB0000000000000002 switch Second",1));
 CHECK(frame(s[1],p,457,0));CHECK(frame(s[1],p,457,35));CHECK(frame(s[1],p,457,15));
 CHECK(expect_control(path,"SET TVB0000000000000001 1",1));CHECK(frame(s[1],p,456,15)&&u32(p+24)==1);
 CHECK(expect_control(path,"ANNOUNCE TVB0000000000000002",1));
 CHECK(frame(s[1],p,457,0));CHECK(frame(s[1],p,457,35));CHECK(frame(s[1],p,457,15)&&u32(p+24)==0);
 CHECK(expect_control(path,"LEVEL TVB0000000000000001 50",0));
 CHECK(expect_control(path,"ADD TVB0000000000000001 switch First",1));
 CHECK(poll(&wait,1,50)==0); /* Idempotent ADD must not reannounce/reset state. */
 CHECK(expect_control(path,"ADD TVB0000000000000001 cover First",0));
 CHECK(expect_control(path,"ADD TVB0000000000000001 switch Duplicate",0));
 CHECK(expect_control(path,"RENAME TVB0000000000000001 Bedroom switch",1));
 CHECK(frame(s[1],p,456,0)&&!strcmp((char*)p+20,"Bedroom switch"));CHECK(frame(s[1],p,456,35));CHECK(frame(s[1],p,456,15));
 CHECK(expect_control(path,"DISABLE TVB0000000000000001",1));
 CHECK(expect_control(path,"SET TVB0000000000000001 0",0));
 CHECK(expect_control(path,"ENABLE TVB0000000000000001",1));
 CHECK(frame(s[1],p,456,0));CHECK(frame(s[1],p,456,35));CHECK(frame(s[1],p,456,15)&&u32(p+24)==1);
 const char *profiles[]={"dimmable_light","color_temperature_light","cover","thermostat"};
 for(unsigned i=0;i<4;i++){
  struct timeval before_config;
  CHECK(gettimeofday(&before_config,NULL)==0);
  uint64_t minimum_revision=(uint64_t)before_config.tv_sec*UINT64_C(1000000)+(uint64_t)before_config.tv_usec;
  char command[160];unsigned id=458+i;
  snprintf(command,sizeof(command),"ADD TVB000000000000000%u %s Test %u",i+3,profiles[i],i);
  CHECK(expect_control(path,command,1));CHECK(frame(s[1],p,id,0));
  if(i<3){
   CHECK(frame(s[1],p,id,95));
   uint64_t parent_revision=u64(p+40);
   CHECK(parent_revision>=minimum_revision&&u32(p+40)!=0);
   snprintf(command,sizeof(command),"UNIT TVB000000000000000%u",i+3);
   CHECK(expect_control(path,command,1));CHECK(frame(s[1],p,id,98));CHECK(paired_unit(p));
   CHECK(u64(p+156)>parent_revision&&u32(p+156)!=0);
   CHECK(u32(p+108)==(i==0?265:i==1?278:281));
   /* The paired payloads cover native create and persisted-update decoding. */
   if(i<2){CHECK(u32(p+112)==0x00020000);CHECK(u32(p+116)==0x01020000);CHECK(u32(p+312)==512);CHECK(u32(p+316)==513);}
   if(i==1){CHECK(u32(p+120)==0x02020000);CHECK(u32(p+320)==514);}
   unsigned count=i==1?4:2;
   for(unsigned j=0;j<count;j++)CHECK(frame(s[1],p,id,118));
  }else{
   CHECK(frame(s[1],p,id,23));CHECK(frame(s[1],p,id,57));CHECK(frame(s[1],p,id,58));
   CHECK(frame(s[1],p,id,117));CHECK(frame(s[1],p,id,55));
  }
 }
 CHECK(expect_control(path,"LEVEL TVB0000000000000003 37",1));CHECK(frame(s[1],p,458,118)&&p[36]==37);
 CHECK(expect_control(path,"COLOR_TEMP TVB0000000000000004 4000",1));CHECK(frame(s[1],p,459,118)&&u16(p+40)==4000);
 CHECK(expect_control(path,"POSITION TVB0000000000000005 25",1));CHECK(frame(s[1],p,460,118)&&p[36]==75);CHECK(frame(s[1],p,460,118));
 CHECK(expect_control(path,"TARGET TVB0000000000000006 nan",0));
 CHECK(expect_control(path,"TARGET TVB0000000000000006 inf",0));
 CHECK(expect_control(path,"TARGET TVB0000000000000006 -inf",0));
 CHECK(expect_control(path,"TARGET TVB0000000000000006 21.5",1));CHECK(frame(s[1],p,461,57)&&p[24]==43);
 CHECK(expect_control(path,"MODE TVB0000000000000006 off",1));CHECK(frame(s[1],p,461,57)&&p[24]==253);
 CHECK(expect_control(path,"TIMER TVB0000000000000006 cancel",1));CHECK(frame(s[1],p,461,117));
 CHECK(expect_control(path,"TIMER TVB0000000000000006 boost 99999999999999999999999999",0));
 CHECK(expect_control(path,"TIMER TVB0000000000000006 boost -1",0));
 CHECK(expect_control(path,"SCHEDULE TVB0000000000000006 active 4294967336 36 100 200",0));
 CHECK(expect_control(path,"SCHEDULE TVB0000000000000006 active 40 36 100 200",1));CHECK(frame(s[1],p,461,57)&&p[24]==253);CHECK(frame(s[1],p,461,55));
 if(!golden_file){
  CHECK(expect_control(path,"TARGET TVB0000000000000006 max",1));CHECK(frame(s[1],p,461,57)&&p[24]==253);
  CHECK(expect_control(path,"MODE TVB0000000000000006 off",1));CHECK(frame(s[1],p,461,57)&&p[24]==253);
  CHECK(expect_control(path,"MODE TVB0000000000000006 heat",1));CHECK(frame(s[1],p,461,57)&&p[24]==254);
  CHECK(expect_control(path,"ACTUAL TVB0000000000000006 21.3",1));CHECK(frame(s[1],p,461,23)&&u32(p+24)==213);CHECK(frame(s[1],p,461,57)&&p[28]==43);
  CHECK(expect_control(path,"ACTUAL TVB0000000000000006 unknown",1));CHECK(frame(s[1],p,461,23)&&u32(p+24)==(uint32_t)-9999);CHECK(frame(s[1],p,461,57)&&p[28]==255);
 }
 int watch=socket(AF_UNIX,SOCK_SEQPACKET,0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,path);
 CHECK(watch>=0&&!connect(watch,(struct sockaddr*)&a,sizeof(a)));
 setsockopt(watch,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 CHECK(send(watch,"WATCH",5,0)==5);char event[2048];CHECK(recv(watch,event,sizeof(event),0)>0);
 unsigned char command[44]={7,3,0,44,0,0,0,3,1,202,0,0,0,0,0,28,0,0,0,118,0,20,0,0,0,1,0,0,0,0,0,110,0,8,0,0,60,0,0,0,0,0,0,1};
 CHECK(send(s[1],command,sizeof(command),0)==sizeof(command));CHECK(frame(s[1],p,458,118)&&p[36]==60);
 ssize_t n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;
 CHECK(strstr(event,"TVB0000000000000003")&&strstr(event,"60"));
 /* Native stream batching must preserve both commands and their order. */
 unsigned char batch[88],passed[89];
 memcpy(batch,command,44);memcpy(batch+44,command,44);
 batch[36]=40;batch[80]=70;
 CHECK(send(s[1],batch,sizeof(batch),0)==sizeof(batch));
 CHECK(frame(s[1],p,458,118)&&p[36]==40);
 CHECK(frame(s[1],p,458,118)&&p[36]==70);
 for(unsigned i=0;i<2;i++){
  n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;
  CHECK(strstr(event,i?"\"level\":70":"\"level\":40"));
 }
 /* An unrelated first frame must pass through without hiding the second. */
 batch[8]=0;batch[9]=100;
 CHECK(send(s[1],batch,sizeof(batch),0)==sizeof(batch));
 CHECK(recv(s[0],passed,44,MSG_WAITALL)==44&&!memcmp(passed,batch,44));
 CHECK(frame(s[1],p,458,118)&&p[36]==70);
 n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;
 CHECK(strstr(event,"\"level\":70"));
 /* No partial consumption if the complete buffer is not valid framing. */
 memcpy(passed,command,44);passed[44]=0;
 CHECK(send(s[1],passed,45,0)==45);
 CHECK(recv(s[0],batch,45,MSG_WAITALL)==45&&!memcmp(batch,passed,45));
 CHECK(poll(&wait,1,50)==0);
 /* Route each remaining profile through the real intercepted send path. */
 unsigned char sw[28]={7,3,0,28,0,0,0,3,1,200,0,0,0,0,0,12,0,0,0,15,0,4,0,0,0,0,0,0};
 CHECK(send(s[1],sw,sizeof(sw),0)==sizeof(sw));CHECK(frame(s[1],p,456,15));
 n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;CHECK(strstr(event,"TVB0000000000000001"));
 unsigned char ct[46]={7,3,0,46,0,0,0,3,1,203,0,0,0,0,0,30,0,0,0,118,0,22,0,0,0,1,0,0,0,0,0,108,0,10,0,0,0,0,0,2,0x0f,0xa0,0,0,0,0};
 if(!golden_file){
  /* Native endpoint cycles are ordinary absolute commands. They and a
   * neighboring step must all reach WATCH and echo the requested value. */
  CHECK(expect_control(path,"COLOR_TEMP TVB0000000000000004 6500",1));
  CHECK(frame(s[1],p,459,118)&&u16(p+40)==6500);
  ct[40]=0x0a;ct[41]=0x8c; /* Native 6500 -> 2700 cycle. */
  CHECK(send(s[1],ct,sizeof(ct),0)==sizeof(ct));
  CHECK(frame(s[1],p,459,118)&&u16(p+40)==2700);
  n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;
  CHECK(strstr(event,"TVB0000000000000004")&&strstr(event,"\"color_temperature\":2700"));
  CHECK(expect_control(path,"COLOR_TEMP TVB0000000000000004 2700",1));
  CHECK(frame(s[1],p,459,118)&&u16(p+40)==2700);
  ct[40]=0x19;ct[41]=0x64; /* Native 2700 -> 6500 cycle. */
  CHECK(send(s[1],ct,sizeof(ct),0)==sizeof(ct));
  CHECK(frame(s[1],p,459,118)&&u16(p+40)==6500);
  n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;
  CHECK(strstr(event,"TVB0000000000000004")&&strstr(event,"\"color_temperature\":6500"));
  ct[40]=0x0b;ct[41]=0xb8; /* An ordinary palette command. */
  CHECK(send(s[1],ct,sizeof(ct),0)==sizeof(ct));
  CHECK(frame(s[1],p,459,118)&&u16(p+40)==3000);
  n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;
  CHECK(strstr(event,"TVB0000000000000004")&&strstr(event,"\"color_temperature\":3000"));
 }
 ct[40]=0x0f;ct[41]=0xa0;
 CHECK(send(s[1],ct,sizeof(ct),0)==sizeof(ct));CHECK(frame(s[1],p,459,118));
 n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;CHECK(strstr(event,"TVB0000000000000004"));
 command[9]=204;command[31]=125;memset(command+36,0,8);command[39]=14;
 CHECK(send(s[1],command,sizeof(command),0)==sizeof(command));CHECK(frame(s[1],p,460,118));
 n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;CHECK(strstr(event,"TVB0000000000000005")&&strstr(event,"stop"));
 unsigned char th[40]={7,3,0,40,0,0,0,3,1,205,0,0,0,0,0,24,0,0,0,57,0,16,0,0,42,255,255,128,255,0,0,0,0,0,0,1,0,0,0,0};
 CHECK(send(s[1],th,sizeof(th),0)==sizeof(th));CHECK(frame(s[1],p,461,57));
 n=recv(watch,event,sizeof(event)-1,0);CHECK(n>0);event[n]=0;CHECK(strstr(event,"TVB0000000000000006"));
 CHECK(expect_control(path,"DISABLE TVB0000000000000001",1));
 CHECK(send(s[1],sw,sizeof(sw),0)==sizeof(sw));
 wait.fd=watch;CHECK(poll(&wait,1,50)==0);wait.fd=s[1];CHECK(poll(&wait,1,50)==0);
 CHECK(expect_control(path,"ANNOUNCE TVB0000000000000003",1));CHECK(frame(s[1],p,458,0));CHECK(frame(s[1],p,458,95));
 CHECK(expect_control(path,"LEVEL TVB0000000000000003 50",0));
 CHECK(expect_control(path,"UNIT TVB0000000000000003",1));CHECK(frame(s[1],p,458,98));CHECK(paired_unit(p));
 CHECK(frame(s[1],p,458,118));CHECK(frame(s[1],p,458,118)&&p[36]==70);
 /* Durable allocation can contain gaps after deletion and process restart. */
 CHECK(expect_control(path,"RESTORE TVB0000000000000010 500 switch Restored",1));
 CHECK(frame(s[1],p,500,0));CHECK(frame(s[1],p,500,35));CHECK(frame(s[1],p,500,15));
 CHECK(expect_control(path,"RESTORE TVB0000000000000011 490 switch Earlier",1));
 CHECK(frame(s[1],p,490,0));CHECK(frame(s[1],p,490,35));CHECK(frame(s[1],p,490,15));
 CHECK(expect_control(path,"RESTORE TVB0000000000000010 500 switch Restored",1));
 CHECK(expect_control(path,"RESTORE TVB0000000000000010 501 switch Restored",0));
 CHECK(expect_control(path,"RESTORE TVB0000000000000012 500 switch Collision",0));
 CHECK(expect_control(path,"RESTORE TVB0000000000000012 65536 switch Invalid",0));
 CHECK(expect_control(path,"ADD TVB0000000000000012 switch Next",1));
 CHECK(frame(s[1],p,501,0));CHECK(frame(s[1],p,501,35));CHECK(frame(s[1],p,501,15));
 close(watch);close(s[0]);close(s[1]);
 if(golden_file){CHECK(fgetc(golden_file)==EOF);fclose(golden_file);}
 puts("PASS dynamic registry: empty startup, all profiles, durable identity gaps, paired interface encoding, state isolation, controls, WATCH");return 0;
}
#undef CHECK

int main(void){
 if(!getenv("AHA_VIRTUAL_LEGACY")||strcmp(getenv("AHA_VIRTUAL_LEGACY"),"1"))return dynamic_test();
 int s[2],watch=-1;unsigned char p[256]={1,3,0,24,0,0,0,3,0,0,0,3,0,2,32,56};
 struct timeval receive_timeout={2,0};
 prctl(PR_SET_NAME,"sR/TX-test",0,0,0);
 if(socketpair(AF_UNIX,SOCK_STREAM,0,s))return 1;
 setsockopt(s[0],SOL_SOCKET,SO_RCVTIMEO,&receive_timeout,sizeof(receive_timeout));
 setsockopt(s[1],SOL_SOCKET,SO_RCVTIMEO,&receive_timeout,sizeof(receive_timeout));
 if(write(s[0],p,24)!=24||recv(s[1],p,24,MSG_WAITALL)!=24)return 2;
 if(recv(s[1],p,168,MSG_WAITALL)!=168||p[0]!=4)return 3;
 if(recv(s[1],p,32,MSG_WAITALL)!=32||p[0]!=7||p[19]!=35)return 4;
 if(recv(s[1],p,28,MSG_WAITALL)!=28||p[27]!=0)return 9;
 if(recv(s[1],p,168,MSG_WAITALL)!=168||p[0]!=4||p[9]!=195)return 13;
 if(recv(s[1],p,32,MSG_WAITALL)!=32||p[19]!=35)return 14;
 if(recv(s[1],p,26,MSG_WAITALL)!=26||p[19]!=114)return 15;
 if(recv(s[1],p,28,MSG_WAITALL)!=28||p[19]!=15||p[27]!=0)return 16;
 if(recv(s[1],p,32,MSG_WAITALL)!=32||p[19]!=110||p[24]!=100||p[31]!=1)return 17;
 if(recv(s[1],p,168,MSG_WAITALL)!=168||p[0]!=4||p[9]!=196)return 26;
 if(recv(s[1],p,48,MSG_WAITALL)!=48||p[19]!=95)return 27;
 if(recv(s[1],p,168,MSG_WAITALL)!=168||p[0]!=4||p[9]!=197)return 40;
 if(recv(s[1],p,48,MSG_WAITALL)!=48||p[19]!=95)return 41;
 if(recv(s[1],p,168,MSG_WAITALL)!=168||p[0]!=4||p[9]!=198)return 59;
 if(recv(s[1],p,48,MSG_WAITALL)!=48||p[19]!=95)return 60;
 if(recv(s[1],p,168,MSG_WAITALL)!=168||p[0]!=4||p[9]!=199||
    p[16]!=0||p[17]!=0||p[18]!=1||p[19]!=0x40)return 70;
 if(recv(s[1],p,32,MSG_WAITALL)!=32||p[19]!=23||
    u32(p+24)!=(uint32_t)(int32_t)-9999)return 71;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=40||
    p[25]!=36||p[26]!=44||p[28]!=255)return 72;
 if(recv(s[1],p,28,MSG_WAITALL)!=28||p[19]!=58||p[27]!=100)return 73;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=117||p[27]!=255)return 84;
 if(recv(s[1],p,32,MSG_WAITALL)!=32||p[19]!=55||u32(p+24)!=1||u16(p+28)!=0||u16(p+30)!=0)return 93;
 const char *control_path=getenv("AHA_VIRTUAL_CONTROL_PATH");
 if(!control_path)control_path="/var/tmp/aha-virtual-provider.ctl";
 for(int i=0;i<100&&access(control_path,F_OK);i++)usleep(10000);
 if(!control_command(control_path,"HANFUN UNIT"))return 28;
 if(recv(s[1],p,216,MSG_WAITALL)!=216||p[19]!=98||
    !(p[160]||p[161]||p[162]||p[163]))return 28;
 if(!control_command(control_path,"HANFUN STATUS"))return 29;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=118||p[31]!=15)return 30;
 if(recv(s[1],p,44,MSG_WAITALL)!=44||p[19]!=118||p[31]!=110)return 31;
 if(!control_command(control_path,"COLOR UNIT"))return 42;
 if(recv(s[1],p,216,MSG_WAITALL)!=216||p[19]!=98||p[9]!=197||
    !(p[160]||p[161]||p[162]||p[163]))return 43;
 if(!control_command(control_path,"COLOR STATUS"))return 44;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=118||p[31]!=109||p[37]!=4)return 45;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=118||p[31]!=15)return 46;
 if(recv(s[1],p,44,MSG_WAITALL)!=44||p[19]!=118||p[31]!=110)return 47;
 if(recv(s[1],p,46,MSG_WAITALL)!=46||p[19]!=118||p[31]!=108||
    p[39]!=2||p[40]!=0x0a||p[41]!=0x8c)return 48;
 if(!control_command(control_path,"COLOR FULL"))return 53;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=118||p[31]!=109||p[37]!=5||p[38]!=1)return 54;
 if(!control_command(control_path,"BLIND UNIT"))return 61;
 if(recv(s[1],p,216,MSG_WAITALL)!=216||p[19]!=98||p[9]!=198||
    p[110]!=1||p[111]!=0x19||p[115]!=1||p[119]!=4||p[123]!=5)return 62;
 if(!control_command(control_path,"BLIND STATUS"))return 63;
 if(recv(s[1],p,44,MSG_WAITALL)!=44||p[19]!=118||p[31]!=110||p[36]!=50)return 64;
 if(recv(s[1],p,42,MSG_WAITALL)!=42||p[19]!=118||p[31]!=125||p[39]!=15||p[41]!=0)return 65;
 if(!control_command(control_path,"TARGET VIRT000000000006 21.5"))return 78;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=43)return 79;
 if(!control_command(control_path,"MODE VIRT000000000006 off"))return 80;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=253)return 81;
 if(!control_command(control_path,"MODE VIRT000000000006 heat"))return 82;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=43)return 83;
 if(!control_command(control_path,"TIMER VIRT000000000006 cold 2000"))return 85;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=117||p[27]!=3||p[30]!=7||p[31]!=0xd0)return 86;
 if(!control_command(control_path,"TIMER VIRT000000000006 cancel"))return 87;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=117||p[27]!=255)return 88;
 if(!control_command(control_path,"SCHEDULE VIRT000000000006 active 44 34 123 456"))return 94;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=43||p[25]!=34||p[26]!=44)return 95;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=55||u32(p+24)!=1||u16(p+28)!=1||u16(p+30)!=2)return 96;
 if(!control_command(control_path,"SCHEDULE VIRT000000000006 disabled"))return 97;
 if(recv(s[1],p,32,MSG_WAITALL)!=32||p[19]!=55||u16(p+28)!=0||u16(p+30)!=0)return 98;
 {
  unsigned char schedule_edit[40]={7,3,0,40,0,0,0,3,1,199,0,0,0,0,0,24,
    0,0,0,55,0,16,0,0,0,0,0,1,0,1,0,2,0,1,0,0x25,0,2,0,4};
  if(send(s[1],schedule_edit,sizeof(schedule_edit),0)!=(ssize_t)sizeof(schedule_edit))return 99;
  if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=43)return 100;
  if(recv(s[1],p,32,MSG_WAITALL)!=32||p[19]!=55||u16(p+28)!=0)return 101;
  unsigned char generated_target[40]={7,3,0,40,0,0,0,3,1,199,0,0,0,0,0,24,
    0,0,0,57,0,16,0,0,34,34,44,0,255,0,0,0,0,0,0,1,0,0,0,0};
  if(send(s[1],generated_target,sizeof(generated_target),0)!=(ssize_t)sizeof(generated_target))return 102;
  if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=43)return 103;
  if(recv(s[1],p,32,MSG_WAITALL)!=32||p[19]!=55||u16(p+28)!=0)return 104;
 }
 struct sockaddr_un a={.sun_family=AF_UNIX};
 strcpy(a.sun_path,control_path?control_path:"/var/tmp/aha-virtual-provider.ctl");
 for(int i=0;i<100;i++){
  watch=socket(AF_UNIX,SOCK_SEQPACKET,0);
  if(watch>=0&&!connect(watch,(struct sockaddr*)&a,sizeof(a)))break;
  if(watch>=0)close(watch);
  watch=-1;usleep(10000);
 }
 if(watch<0)return 10;
 setsockopt(watch,SOL_SOCKET,SO_RCVTIMEO,&receive_timeout,sizeof(receive_timeout));
 if(send(watch,"WATCH\n",6,0)!=6||recv(watch,p,sizeof(p),0)<=0)return 10;
 unsigned char command[28]={7,3,0,28,0,0,0,3,1,194,0,0,0,0,0,12,0,0,0,15,0,4,0,0,0,0,0,1};
 if(send(s[1],command,28,0)!=28)return 5;
 if(recv(s[1],p,28,MSG_WAITALL)!=28||p[27]!=1)return 6;
 ssize_t event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 11;
 p[event_length]=0;
 if(!strstr((char*)p,"\"event\":\"command\"")||!strstr((char*)p,"\"state\":1")||
    !strstr((char*)p,"VIRT000000000001"))return 12;
 command[9]=195;
 if(send(s[1],command,28,0)!=28)return 18;
 if(recv(s[1],p,28,MSG_WAITALL)!=28||p[27]!=1)return 19;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 20;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000002")||!strstr((char*)p,"\"state\":1"))return 21;
 unsigned char level[32]={7,3,0,32,0,0,0,3,1,195,0,0,0,0,0,16,0,0,0,110,0,8,0,0,128,0,0,0,0,0,0,0};
 if(send(s[1],level,32,0)!=32)return 22;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 24;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000002")||!strstr((char*)p,"\"level\":50"))return 25;
 unsigned char hanfun_relay[40]={7,3,0,40,0,0,0,3,1,196,0,0,0,0,0,24,
   0,0,0,118,0,16,0,0,0,1,0,0,0,0,0,15,0,4,0,0,0,0,0,1};
 if(send(s[1],hanfun_relay,sizeof(hanfun_relay),0)!=(ssize_t)sizeof(hanfun_relay))return 32;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=118||p[31]!=15||p[39]!=1)return 33;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 34;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000003")||!strstr((char*)p,"\"state\":1"))return 35;
 unsigned char hanfun_level[44]={7,3,0,44,0,0,0,3,1,196,0,0,0,0,0,28,
   0,0,0,118,0,20,0,0,0,1,0,0,0,0,0,110,0,8,0,0,128,0,0,0,0,0,0,0};
 if(send(s[1],hanfun_level,sizeof(hanfun_level),0)!=(ssize_t)sizeof(hanfun_level))return 36;
 if(recv(s[1],p,44,MSG_WAITALL)!=44||p[19]!=118||p[31]!=110||p[36]!=50)return 37;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 38;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000003")||!strstr((char*)p,"\"level\":50"))return 39;
 unsigned char color_temperature_command[46]={7,3,0,46,0,0,0,3,1,197,0,0,0,0,0,30,
   0,0,0,118,0,22,0,0,0,1,0,0,0,0,0,108,0,10,0,0,0,0,0,2,0x0f,0xa0,0,0,0,0};
 if(send(s[1],color_temperature_command,sizeof(color_temperature_command),0)!=(ssize_t)sizeof(color_temperature_command))return 49;
 ssize_t color_reply=recv(s[1],p,46,MSG_WAITALL);
 if(color_reply!=46||p[19]!=118||p[31]!=108||p[39]!=2||p[40]!=0x0f||p[41]!=0xa0){
  fprintf(stderr,"color reply bytes=%zd function=%u inner=%u mode=%u temp=%02x%02x\n",
    color_reply,p[19],p[31],p[39],p[40],p[41]);return 50;
 }
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 51;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000004")||!strstr((char*)p,"\"color_temperature\":4000"))return 52;
 unsigned char hs_command[52]={7,3,0,52,0,0,0,3,1,197,0,0,0,0,0,36,
   0,0,0,118,0,28,0,0,0,1,0,0,0,0,0,108,0,16,0,0,
   0,0,0,0,0,120,255,0,1,0,0,0,0,0,0,0};
 if(send(s[1],hs_command,sizeof(hs_command),0)!=(ssize_t)sizeof(hs_command))return 55;
 if(recv(s[1],p,52,MSG_WAITALL)!=52||p[19]!=118||p[31]!=108||p[39]!=0||p[41]!=120||p[42]!=255||p[44]!=1)return 56;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 57;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000004")||!strstr((char*)p,"\"color_mode\":\"hs\"")||
    !strstr((char*)p,"\"hue\":120")||!strstr((char*)p,"\"saturation\":255"))return 58;
 unsigned char cover_open[44]={7,3,0,44,0,0,0,3,1,198,0,0,0,0,0,28,
   0,0,0,118,0,20,0,0,0,1,0,0,0,0,0,125,0,8,0,0,0,0,0,12,0,0,0,0};
 if(send(s[1],cover_open,sizeof(cover_open),0)!=(ssize_t)sizeof(cover_open))return 66;
 if(recv(s[1],p,42,MSG_WAITALL)!=42||p[19]!=118||p[31]!=125||p[39]!=15||p[41]!=0)return 67;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 68;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000005")||!strstr((char*)p,"\"action\":\"open\"")||
    !strstr((char*)p,"\"position\":50"))return 69;
 unsigned char thermostat_command[40]={7,3,0,40,0,0,0,3,1,199,0,0,0,0,0,24,
   0,0,0,57,0,16,0,0,42,255,255,128,255,0,0,0,0,0,0,1,0,0,0,0};
 if(send(s[1],thermostat_command,sizeof(thermostat_command),0)!=(ssize_t)sizeof(thermostat_command))return 74;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=42)return 75;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 76;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000006")||!strstr((char*)p,"\"target_temperature\":21.0"))return 77;
 if(!golden_file){
  thermostat_command[24]=254;
  if(send(s[1],thermostat_command,sizeof(thermostat_command),0)!=(ssize_t)sizeof(thermostat_command))return 78;
  if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=57||p[24]!=254)return 79;
  event_length=recv(watch,p,sizeof(p)-1,0);if(event_length<=0)return 80;p[event_length]=0;
  if(!strstr((char*)p,"VIRT000000000006")||!strstr((char*)p,"\"target_temperature\":\"max\""))return 81;
 }
 unsigned char thermostat_timer[40]={7,3,0,40,0,0,0,3,1,199,0,0,0,0,0,24,
   0,0,0,117,0,16,0,0,0,0,0,1,0,0,3,0xe8,0,0,0,100,255,0x20,2,0};
 if(send(s[1],thermostat_timer,sizeof(thermostat_timer),0)!=(ssize_t)sizeof(thermostat_timer))return 89;
 if(recv(s[1],p,40,MSG_WAITALL)!=40||p[19]!=117||p[27]!=1||p[30]!=3||p[31]!=0xe8)return 90;
 event_length=recv(watch,p,sizeof(p)-1,0);
 if(event_length<=0)return 91;
 p[event_length]=0;
 if(!strstr((char*)p,"VIRT000000000006")||!strstr((char*)p,"\"timer_action\":\"boost\"")||
    !strstr((char*)p,"\"end_time\":1000")||!strstr((char*)p,"\"duration\":900"))return 92;
 command[9]=193;
 if(send(s[1],command,28,0)!=28)return 7;
 if(recv(s[0],p,28,MSG_WAITALL)!=28||memcmp(p,command,28))return 8;
 puts("PASS switch/dim/color/blind/thermostat announcements, metadata, WATCH events, feedback, passthrough");
 close(watch);close(s[0]);close(s[1]);return 0;
}
