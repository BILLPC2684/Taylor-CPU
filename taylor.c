#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
 #include<windows.h>
#else
 #include<unistd.h>
#endif

#define true    1
#define false   0
#define SIZ8MB  0x0800000
#define RAMSIZ  0x7FBFE00
#define VRAMSIZ 0x4000000

char* ErrorTexts[3] = {"Warning", "Error", "Fatal"};
int TGRsock, client, server, BufferLength = 1024*1024*8;
char buffer[1024*1024*3];
struct sockaddr_un serveraddr;

typedef struct {
 bool Debug,pause,AsService,blockDisp;
 uint8_t *MEM,PageID[2],ROMBANK[33][SIZ8MB],ErrorType;
 uint16_t Clock;
 uint64_t TI, tmp[8];
 float IPS;
 char *REG, *Error, *BN, *RN, *SN;
 //TI:    Total Instructions
 //FN:    BIOS FileName
 //RN:    ROM  FileName
 //SN:    SAV  FileName
} System;
System sys;
typedef struct {
 int16_t X[2],Y[2];
 uint8_t R,G,B,A,Layer,Rez;
 bool running,reset;
 uint32_t IP,sp,SP,BP,MP,IPS,E,I,O,U;
 // 0, 1, . . . X[2]    16-bit X Position / X2/Width Position
 // 2, 3, . . . Y[2]    16-bit Y Position / Y2/Height Position
 // 4 . . . . . IP      28-bit Instuction Pointer
 // 5 . . . . . SP      20-bit Stack Pointer
 // 6 . . . . . sp:     28-bit Sprite Pointer
 // 7, 8, 9,10, R,G,B,A  8-bit Red, Green, Blue, Alpha
 //11,12,13,14, E,I,O,U 28-bit General Purpose Registers

 //   BP:  BasePoint  (Stack)
 //   MP:  MaxPointer (Stack)
} GPU_INIT;
GPU_INIT GPU;
void GPU_REGW(uint8_t ID, uint64_t Data) {
 if(ID== 0){GPU.X[0]=Data;}else if(ID== 1){GPU.X[1]=Data;}else if(ID== 2){GPU.Y[0]=Data;}else if(ID== 3){GPU.Y[1]=Data;}else if(ID== 4){GPU.IP=Data;}else if(ID== 5){printf("[GPU ERROR] StackPointer Can not be written to!\n");}else if(ID== 6){GPU.sp=Data;}else
 if(ID== 7){GPU.R=Data;}else if(ID== 8){GPU.G=Data;}else if(ID== 9){GPU.B=Data;}else if(ID==10){GPU.A=Data;}else if(ID==11){GPU.E=Data;}else if(ID==12){GPU.I=Data;}else if(ID==13){GPU.O=Data;}else if(ID==14){GPU.U=Data;}
 else{printf("[GPU ERROR] Unknown Regster ID: %i",ID);}
}
int32_t GPU_REGR(uint8_t ID) {
 int32_t a[15] = {GPU.X[0],GPU.X[1],GPU.Y[0],GPU.Y[1],GPU.IP,GPU.SP,GPU.sp,GPU.R,GPU.G,GPU.B,GPU.A,GPU.E,GPU.I,GPU.O,GPU.U};
 if(ID > 14){printf("[GPU ERROR] Unknown Regster ID: %i",ID);return 0;} else {return a[ID];}
} char GPU_REGN[16][2] = {"X ","X2","Y ","Y2","IP","SP","sp","R ","G ","B ","A ","E ","I ","O ","U ","__"};
char* GPU_REG(uint8_t ID) {
 if(ID > 14){printf("[GPU ERROR] Unknown Regster ID: %i",ID);return GPU_REGN[15];} else {return GPU_REGN[ID];}
}
typedef struct {
 uint16_t REGs[8];
 bool running,flag[8],Debug,ticked,reset;
 uint32_t IP,SP,BP,MP;
 uint64_t IPS,TI,time;
 //IP:    InstuctionPointer
 //SP:    StackPointer
 //BP:    BasePoint
 //MP: Max Value of StackPointer
 //IPS:   InstructionsPerSecond
 //TI:    Total Instructions
} CPU_INIT;
CPU_INIT CPU[2];

void ResetCore(bool ID);
void ClientCore(bool ID);
void LoadPage(bool PID, uint8_t BID);

char* concat(const char *s1, const char *s2) {
 const size_t len[2] = {strlen(s1), strlen(s2)};
 char *result = malloc(len[0]+len[1]+1);//+1 for the null-terminator
 //in real code you would check for errors in malloc here
 memcpy(result, s1, len[0]);
 memcpy(result+len[0], s2, len[1]+1);//+1 to copy the null-terminator
 return result;
}

void bin_dump(uint64_t u) { uint8_t i=63; while (i-->0) { printf("%hhu",(u&(uint64_t)pow(2,i))?1:0); } printf("\n"); }

char *ascii127 = "................................ !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~.......................................................................................................................................................";
void dumpData(char *name, unsigned char *data, uint32_t size, uint32_t start, uint32_t end) {
 uint8_t bytes[16]; int i,k,j=1,l=0,m=0;
 printf("._______._______________________________________________.________________.\n|%s",name);
 for(i=0;i<7-strlen(name);i++) { printf(" "); }
 printf("|00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F|0123456789ABCDEF|\n|-------|-----------------------------------------------|----------------|\n|%07X|",start);
 for (i=start;i<start+end;i++) {
  if (i >= start+size) { break; }
  if (j > 15) {
   bytes[j-1] = data[i];
   printf("%02X|",data[i]);
   for (k=0; k < 16; k++) {
    printf("%c",ascii127[bytes[k]]);
   } l=0,j=0;
   printf("|\n|%07X|",i+1);
  } else {
   printf("%02X ",data[i]);
   bytes[j-1] = data[i];
  } j++; l++;
 }m=i;
 if (j > 0) {
  for (i=j; i < 16; i++) {
   printf("-- ");
   bytes[j-1] = 0x00;
  } printf("--|");
  for (i=0; i < l-1; i++) {
   printf("%c",ascii127[bytes[i]]);
  }
  if(m<16) { printf("%c",ascii127[bytes[m-1]]); }
  for (i=j; i < 16; i++) {
   printf(" ");
  } printf(" |\n");
 } printf("|_______|_______________________________________________|________________|\n\\Size: 0x%x/%d Bytes(",size,size);
 if (size < 1024) { printf("%0.2f KB)\n",size/1024.0f); } else { printf("%0.2f MB)\n",(float)size/1024.0f/1024.0f); }
}
void crop(char *dst, char *src, size_t mn, size_t mx) {
 const int len=mx-mn; src+=mn;
 for (int i=0; i<len; i++) { dst[i]=src[i]; }
 dst[len]='\0';
}

FILE *BIOSfp,*ROMfp,*SAVfp,Statefp;

void FlushSAV() {
 char SN[strlen(sys.RN)+1]; crop(SN, sys.RN, 0, strlen(sys.RN)-4); strcat(SN, ".sav");
 SAVfp = fopen(SN,"wb"); fwrite(&sys.MEM[0x1000000], 1, SIZ8MB, SAVfp);
 fflush(SAVfp); fclose(SAVfp);
}
void WriteSAV(uint32_t Address, uint8_t Data) {
 SAVfp = fopen(sys.SN,"wb"); fseek(Address, SAVfp, SEEK_SET);
 fwrite(Data, 1, 1, SAVfp); fflush(SAVfp); fclose(SAVfp);
}

int8_t LoadCart() {
 if ((ROMfp = fopen(sys.RN,"rb")) == NULL) { printf("Error! Failed to access ROM file\n"); return -1; }
 for(uint8_t i=0;i<32;i++){ if(feof(ROMfp)){break;} fread(&sys.ROMBANK[i], SIZ8MB, 1, ROMfp); }
 fclose(ROMfp);
 char SN[strlen(sys.RN)+1]; crop(SN, sys.RN, 0, strlen(sys.RN)-4); strcat(SN, ".sav");
 if ((SAVfp = fopen(SN,"rb")) != NULL) { fread(&sys.MEM[0x1000000], 1, SIZ8MB, SAVfp); fclose(SAVfp);}
 FlushSAV();
 return 0;
}

void socksend(char* data) {
 send(TGRsock,data,sizeof(data),MSG_DONTWAIT);
}

void printError() {
 if((strlen(sys.Error)>0) && (sys.ErrorType<3)) {
  printf("[EMU %s] %s\n\n",ErrorTexts[sys.ErrorType],sys.Error); memset(sys.Error, 0, sizeof(sys.Error)); sys.ErrorType = 1;
  sys.ErrorType = 3;
}}

void sendError() {
 if((strlen(sys.Error)>0) && (sys.ErrorType<3)) {
  char msg[2048] = {0};
  sprintf(msg,"[EMU %s] %s\n",ErrorTexts[sys.ErrorType],sys.Error); memset(sys.Error, 0, sizeof(sys.Error)); sys.ErrorType = 1;
  socksend(msg); sys.ErrorType = 3;
}}

void Clock(uint32_t *i);
int main(int argc, char *argv[]) { //############################################//
 sys.Debug = true; //debug mode
 sys.blockDisp = false; //block DISP instuction messages
 
 if((sys.MEM = malloc(0xD800000)) == NULL) { printf("Memory allocation failed.\n"); return -1;}
 memset(sys.MEM, 0, 0xD800000);
 sys.Clock = 0;
 sys.REG = "ABCDEFGH________";
 sys.Error = malloc(1024);
 sys.BN = malloc(1024); sys.RN = malloc(1024); sys.SN = malloc(1024);
 for(uint8_t i=0;i<33;i++){ memset(sys.ROMBANK[i], 0, SIZ8MB); }
 pthread_t call_Core0; pthread_create(&call_Core0, NULL, ClientCore, 0);
 pthread_t call_Core1; pthread_create(&call_Core1, NULL, ClientCore, 1);
 for(int i=0x9800000;i<2+(0xFFFF*11);i++){sys.MEM[i]=0;}
 GPU.SP=0xD7FFFFF; GPU.BP=0xD780000; GPU.MP=0xD7FFFFF;
 if(argc < 2) { // perror("Please Provide a Path to the UNIX File\n");
  sys.AsService = false;
  sys.RN = "./fib-endless.tgr";
  
  printf("Starting Taylor v0.28 Alpha Build\n\\with ROM: %s\n",sys.RN);
  if(LoadCart()<0) { return -1; } ResetCore(0);ResetCore(1);
  LoadPage(0,0); LoadPage(1,1); //Feeding the ROM to Taylor
  dumpData("ROM - 0", sys.ROMBANK[0], SIZ8MB, 0, 256);
  
  printf("Header: \""); for(uint8_t i=0;i<5;i++){printf("%c",ascii127[sys.MEM[i]]);}
  printf("\"\nTitle: \""); for(uint8_t i=0;i<16;i++){printf("%c",ascii127[sys.MEM[0x05+i]]);}
  printf("\"\nversion: \""); for(uint8_t i=0;i<12;i++){printf("%c",ascii127[sys.MEM[0x15+i]]);}
  if (sys.MEM[0]>0) { printf("\"\nAuthor: \""); for(uint8_t i=0;i<32;i++){printf("%c",ascii127[sys.MEM[0x21+i]]);} }
  if (sys.MEM[0]>1) { printf("\"\nCheckSum: \""); for(uint8_t i=0;i<32;i++){printf("%c",ascii127[sys.MEM[0x41+i]]);} }
  printf("\"\n");
  
  CPU[0].running = true;
  uint32_t i=0;
  while(true) {
//   printf("CPU[0].running: %s\n",(CPU[0].running)?"true":"false");
   if((CPU[0].running==false)&&(CPU[1].running==false)&&(sys.ErrorType==3)) { printf("CPU is not Running! TotalRan: %ld|%ld (%ld)\nExiting...\n",CPU[0].TI,CPU[1].TI,sys.TI); break; }
   Clock(&i);
  }
  dumpData("ROM#0", &sys.MEM[0x0000000], 0x0800000, 0, 256);
  dumpData("ROM#1", &sys.MEM[0x0800000], 0x0800000, 0, 256);
  dumpData("RAM",   &sys.MEM[0x1800000], 0x7EFFBFF, 0, 256);
 
 } else { //// BELOW IS UNFINISHED AND NOT TESTED!!!! ////
  sys.AsService = true;
  if ((TGRsock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) { perror("socket() failed"); return -1; }
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sun_family = AF_UNIX;
  strcpy(serveraddr.sun_path, argv[1]);
  if ((server = bind(TGRsock, (struct sockaddr *)&serveraddr, SUN_LEN(&serveraddr))) < 0) { perror("bind() failed"); return -1; }
  if ((server = listen(TGRsock, 10)) < 0) { perror("listen() failed"); return -1; }
  printf("Ready for client connect().\n");
  if ((client = accept(TGRsock, NULL, NULL)) < 0) { perror("accept() failed!"); return -1; }
  if ((server = setsockopt(client, SOL_SOCKET, SO_RCVLOWAT, (char *)&BufferLength, sizeof(BufferLength))) < 0) { perror("setsockopt(SO_RCVLOWAT) failed"); return -1; }
  while(true) {
   while(true) {
    memset(&buffer, 0, sizeof(buffer));
    recv(client, buffer, 1, 0); if(buffer[0] == '\x00'){break;}
    recv(client, buffer, buffer[0], 0);
    if      (strstr(buffer, "ping"  )!=NULL) { printf("ping!\n"); send(client, "pong", 4, 0); }
    else if (strstr(buffer, "init"  )!=NULL) { ResetCore(buffer[5]%2); }
    else if (strstr(buffer, "start" )!=NULL) { CPU[buffer[6]%2].running=true; }
    else if (strstr(buffer, "stop"  )!=NULL) { CPU[buffer[6]%2].running=false; }
    else if (strstr(buffer, "pause" )!=NULL) { sys.pause=buffer[6]%2; }
    else if (strstr(buffer, "frame" )!=NULL) {
     uint8_t Layer = recv(client, buffer, 1, 0);
     uint32_t address = sys.MEM[0x9800000+4+32*16+((Layer==0)?0:Layer*2-1)]<<8|sys.MEM[0x9800000+4+32*16+((Layer==0)?0:Layer*2-1)+1];
     for(int i=0;i<(sys.MEM[0x9800000]<<8|sys.MEM[0x9800001])*(sys.MEM[0x9800002]<<8|sys.MEM[0x9800003]);i++){
      int a = address+GPU.sp;
   }}}
   //GPU WORK//
   //int16_t X[2],Y[2];
   //uint8_t R,G,B,A,Layer,Rez;
   //bool running,reset;
   //uint32_t IP,sp,SP,BP,MP,IPS,E,I,O,U;
   //////////////////
   //GPU_REG(ID)
   //GPU_REGR(ID)
   //GPU_REGW(ID, Data)
   memset(&buffer, 0, sizeof(buffer));
   uint8_t A   =  sys.MEM[0x9800000+GPU.IP+1] >> 4 ;       //4 \___-> A/B = 1   bytes
   uint8_t B   =  sys.MEM[0x9800000+GPU.IP+1] & 0xF;       //4 /\\'
   int32_t IMM = (sys.MEM[0x9800000+GPU.IP+2] & 0xF) << 8; //4  |+--> IMM = 3   bytes
   IMM  = (IMM |  sys.MEM[0x9800000+GPU.IP+3]) << 8;       //8  ||--> BMM = 3.5 bytes
   IMM  = (IMM |  sys.MEM[0x9800000+GPU.IP+4]) << 8;       //8  /|
   int32_t BMM = (B<<24) | IMM;                      //8   /'
   if(sys.Debug == true) {
    printf("\n[GPU] IC: 0x%07X/%9X (Area #",0x9800000+GPU.IP,0x9800000+GPU.IP);
    for (uint8_t i=0; i < 6; i++) { printf("0x%02X",sys.MEM[0x9800000+GPU.IP+i]); if (i < 5) { printf(", "); } }
    printf("] | [A:%s, B:%s, IMM:0x%07X, BMM:0x%07X]\n\\REGs: [",GPU_REG(A),GPU_REG(B),IMM,BMM);
    for (uint8_t i=0; i < 8; i++) { printf("%s:0x%04x",GPU_REG(i),GPU_REGR(i)); if (i < 7) { printf(", "); } }
    printf("]\nSpritePointer: 0x%x/%d\n\\StackPointer: 0x%x/%d | StackBase: 0x%x/%d\n\\\\StackData:[",0x9800000+GPU.sp,0x9800000+GPU.sp,0x9800000+GPU.SP,0x9800000+GPU.SP,0x9800000+GPU.BP,0x9800000+GPU.BP);
   }
   switch(sys.MEM[0x9800000+GPU.IP]) {
    case 0x00:// LOAD   |
     if (sys.Debug == true) { printf("LOAD\n"); }
     GPU_REGW(A, BMM);
    case 0x01:// MOV    |
     if (sys.Debug == true) { printf("MOV\n"); }
     GPU_REGW(A, GPU_REGR(B));
   }
   send(client, buffer, sizeof(buffer), 0);
  }
  if (TGRsock != -1){ close(TGRsock); }
  if (client != -1) { close(client);}
  unlink(argv[1]);
 } return -1;
}

int CARTINIT() { LoadPage(0,0);
 switch(sys.MEM[0]) {
  case 0x00:
   //TGRHeader[5]+Title[16]+version[12]
   return 5+16+12+1;
  case 0x01:
   //TGRHeader[5]+Title[16]+version[12]+Author[32]
   return 5+16+12+32+1;
  case 0x02:
   //TGRHeader[5]+Title[16]+version[12]+Author[32]+CheckSum[32]
   return 5+16+12+32+32+1;
 } 
}

void ResetCore(bool ID) {
 uint32_t*MP    = {0x97FFDFF,0x97DFDFF},
         *SPMIN = {0x97DFE00,0x97BFE00};
 for(uint8_t i=0;i<8;i++){CPU[ID].REGs[i]=0; CPU[ID].flag[i]=false;}
 CPU[ID].running=false; CPU[ID].ticked=false;
 CPU[ID].reset=false; sys.pause=false; CPU[ID].IP=CARTINIT();
 CPU[ID].SP=&MP[ID];
 CPU[ID].BP=&SPMIN[ID];
 CPU[ID].MP=&MP[ID];
 CPU[ID].IPS=0; CPU[ID].TI=0; CPU[ID].time=0;
}

void LoadPage(bool PID, uint8_t BID) {
 sys.PageID[PID]=BID%0x21;
 memcpy(&sys.MEM[PID*SIZ8MB], &sys.ROMBANK[BID%0x21], SIZ8MB);
}

void Clock(uint32_t *i) {
 usleep(1000); *i++;
 if(*i%1000) { //1000 McS = 1 MS
  sys.Clock++;
  if((sys.Clock%1000)==0){
   sys.IPS = (((CPU[0].IPS+CPU[1].IPS)/2.0f)/24000000.0f)*100;
   printf("\nInstuctionsPerSecond: %8ld|%8ld (%8ld) -> %.4f%%\n\\                     24000000|24000000 (48000000) -> 100.0000%%\n\\TotalRan: %ld|%ld (%ld)\n\n",CPU[0].IPS, CPU[1].IPS, CPU[0].IPS+CPU[1].IPS, sys.IPS,CPU[0].TI,CPU[1].TI,sys.TI);
   CPU[0].ticked=1; CPU[1].ticked=1;
}}}

void ClientCore(bool ID) {
 char msg[1024]={0};
 uint16_t dslp = 0;
 int i=0;
 while(true) {
  if(CPU[ID].ticked) { CPU[ID].IPS = 0; CPU[ID].ticked = 0; }
  if(CPU[ID].running && ~sys.pause) { CPU[ID].IP=CPU[ID].IP%0xD800000;
   uint8_t A   =  sys.MEM[CPU[ID].IP+1] >> 4 ;       //4 \.
   uint8_t B   =  sys.MEM[CPU[ID].IP+1] & 0xF;       //4 |-> A/B/C = 1.5 bytes
   uint8_t C   =  sys.MEM[CPU[ID].IP+2] >> 4 ;       //4 /'
   int32_t IMM = (sys.MEM[CPU[ID].IP+2] & 0xF) << 8; //4 \.
   IMM  = (IMM |  sys.MEM[CPU[ID].IP+3]) << 8;       //8 |->  IMM  = 3.5 bytes
   IMM  = (IMM |  sys.MEM[CPU[ID].IP+4]) << 8;       //8 |
   IMM |=         sys.MEM[CPU[ID].IP+5];             //8 /'
   if (sys.Debug == true) {
    sprintf(msg,"%s\n[Core#%x] IC: 0x%07X/%9X (Area ",msg,ID,CPU[ID].IP,CPU[ID].IP);
    if      (CPU[ID].IP>=0x0000000 && CPU[ID].IP<=0x07FFFFF) { sprintf(msg,"%s0:ROM PAGE#0",msg); }
    else if (CPU[ID].IP>=0x0800000 && CPU[ID].IP<=0x0FFFFFF) { sprintf(msg,"%s1:ROM PAGE#1",msg); }
    else if (CPU[ID].IP>=0x1000000 && CPU[ID].IP<=0x17FFFFF) { sprintf(msg,"%s2:SAV data",msg); }
    else if (CPU[ID].IP>=0x1800000 && CPU[ID].IP<=0x96FFBFF) { sprintf(msg,"%s3:Work RAM",msg); }
    else if (CPU[ID].IP>=0x96FFC00 && CPU[ID].IP<=0x97FFBFF) { sprintf(msg,"%s4:Stack Memory",msg); }
    else if (CPU[ID].IP>=0x97FFC00 && CPU[ID].IP<=0x97FFFFF) { sprintf(msg,"%s5:Static Memory",msg); }
    else if (CPU[ID].IP>=0x9800000 && CPU[ID].IP<=0xD77FFFF) { sprintf(msg,"%s6:Video RAM",msg); }
    else if (CPU[ID].IP>=0xD780000 && CPU[ID].IP<=0xD7FFFFF) { sprintf(msg,"%s7:Stack VMem",msg); }
    else { sprintf(msg,"%s?:Invalid Address",msg); } sprintf(msg,"%s)\n\\ >> [",msg);
    for (i=0; i < 6; i++) { sprintf(msg,"%s0x%02X",msg,sys.MEM[CPU[ID].IP+i]); if (i < 5) { sprintf(msg,"%s, ",msg); } }
    sprintf(msg,"%s] | [A:%c, B:%c, C:%c, IMM:0x%07X]\n\\REGs: [",msg,sys.REG[A],sys.REG[B],sys.REG[C],IMM);
    for (i=0; i < 8; i++) { sprintf(msg,"%s%c:0x%04X%s",msg,sys.REG[i],CPU[ID].REGs[i],(i<7)?", ":""); }
//    sprintf(msg,"%s%c:0x%04X, ",msg,sys.REG[0],CPU[ID].REGs[0]); sprintf(msg,"%s%c:0x%04X, ",msg,sys.REG[1],CPU[ID].REGs[1]); sprintf(msg,"%s%c:0x%04X, ",msg,sys.REG[2],CPU[ID].REGs[2]); sprintf(msg,"%s%c:0x%04X, ",msg,sys.REG[3],CPU[ID].REGs[3]); sprintf(msg,"%s%c:0x%04X, ",msg,sys.REG[4],CPU[ID].REGs[4]); sprintf(msg,"%s%c:0x%04X, ",msg,sys.REG[5],CPU[ID].REGs[5]); sprintf(msg,"%s%c:0x%04X, ",msg,sys.REG[6],CPU[ID].REGs[6]); sprintf(msg,"%s%c:0x%04X"  ,msg,sys.REG[7],CPU[ID].REGs[7]);
    sprintf(msg,"%s] | TotalRan: %ld (%ld)\n\\StackPointer: 0x%x/%d | StackBase: 0x%x/%d\n\\\\StackData:[",msg,CPU[ID].TI,sys.TI,CPU[ID].SP,CPU[ID].SP,CPU[ID].BP,CPU[ID].BP);
    for (i = CPU[ID].SP+1; i <= CPU[ID].BP; ++i){
     if((i+1)%2==0) { sprintf(msg,"%s 0x",msg); }
     sprintf(msg,"%s%02X",msg,sys.MEM[i]);
     if(i%16==0 && i != 0) { sprintf(msg,"%s\n",msg); }
    } sprintf(msg,"%s]\n \\instruction: ",msg);
   }
   //Flags | WrittenREG, ReadREG, OverFlow, PointerOOB, ALUoperated, DivideBy0
//   printf("sys.MEM[CPU[ID].IP]: 0x%02X\n",sys.MEM[CPU[ID].IP]);
   switch(sys.MEM[CPU[ID].IP]) {
    case 0x00:// LOAD   |
     if (sys.Debug == true) { sprintf(msg,"%sMOV\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true;
     if (C > 0) { CPU[ID].REGs[A] = CPU[ID].REGs[B]; CPU[ID].flag[1]=true; }
     else { CPU[ID].REGs[A] = IMM; } break;
    case 0x01:// ADD    |
     if (sys.Debug == true) { sprintf(msg,"%sADD\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]+IMM;             if(CPU[ID].REGs[A]+IMM            >0xFFFF){CPU[ID].flag[2]=true;}}
     else {         CPU[ID].REGs[C] = CPU[ID].REGs[A]+CPU[ID].REGs[B]; if(CPU[ID].REGs[A]+CPU[ID].REGs[B]>0xFFFF){CPU[ID].flag[2]=true;}} break;
    case 0x02:// SUB    |
     if (sys.Debug == true) { sprintf(msg,"%sSUB\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[2]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]-IMM;             if(CPU[ID].REGs[A]-IMM            <0){CPU[ID].flag[2]=false;}}
     else {         CPU[ID].REGs[C] = CPU[ID].REGs[A]-CPU[ID].REGs[B]; if(CPU[ID].REGs[A]-CPU[ID].REGs[B]<0){CPU[ID].flag[2]=false;}} break;
    case 0x03:// MUL    |
     if (sys.Debug == true) { sprintf(msg,"%sMUL\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]*IMM;             if(CPU[ID].REGs[A]*IMM            >0xFFFF){CPU[ID].flag[2]=true;}}
     else {         CPU[ID].REGs[C] = CPU[ID].REGs[A]*CPU[ID].REGs[B]; if(CPU[ID].REGs[A]*CPU[ID].REGs[B]>0xFFFF){CPU[ID].flag[2]=true;}} break;
    case 0x04:// DIV    |
     if (sys.Debug == true) { sprintf(msg,"%sDIV\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[2]=true; CPU[ID].flag[4]=true;
     if (CPU[ID].REGs[A]==0) {CPU[ID].REGs[C] = 0; CPU[ID].flag[5]=true; break;}
     if (IMM >= 1) { CPU[ID].REGs[C] = CPU[ID].REGs[A]/IMM;             if(CPU[ID].REGs[A]%IMM            >0){CPU[ID].flag[2]=false;}}
     else {          CPU[ID].REGs[C] = CPU[ID].REGs[A]/CPU[ID].REGs[B]; if(CPU[ID].REGs[A]%CPU[ID].REGs[B]>0){CPU[ID].flag[2]=false;}} break;
    case 0x05:// MOD    |
     if (sys.Debug == true) { sprintf(msg,"%sMOD\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (CPU[ID].REGs[A]==0) {CPU[ID].REGs[C] = 0; CPU[ID].flag[5]=true; break;}
     if (IMM >= 1) { CPU[ID].REGs[C] = CPU[ID].REGs[A]%IMM;             }
     else {          CPU[ID].REGs[C] = CPU[ID].REGs[A]%CPU[ID].REGs[B]; } break;
    case 0x06:// AND    |
     if (sys.Debug == true) { sprintf(msg,"%sAND\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]&IMM; }else{ CPU[ID].REGs[C] = CPU[ID].REGs[A]&CPU[ID].REGs[B]; } break;
    case 0x07:// OR     |
     if (sys.Debug == true) { sprintf(msg,"%sOR\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]|IMM; }else{ CPU[ID].REGs[C] = CPU[ID].REGs[A]|CPU[ID].REGs[B]; } break;
    case 0x08:// XOR    |
     if (sys.Debug == true) { sprintf(msg,"%sXOR\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]^IMM; }else{ CPU[ID].REGs[C] = CPU[ID].REGs[A]^CPU[ID].REGs[B]; } break;
    case 0x09:// BSL    |
     if (sys.Debug == true) { sprintf(msg,"%sBSL\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]<<IMM;             if(CPU[ID].REGs[A]<<IMM            >0xFFFF){CPU[ID].flag[2]=true;}}
     else {         CPU[ID].REGs[C] = CPU[ID].REGs[A]<<CPU[ID].REGs[B]; if(CPU[ID].REGs[A]<<CPU[ID].REGs[B]>0xFFFF){CPU[ID].flag[2]=true;}} break;
    case 0x0A:// BSR    |
     if (sys.Debug == true) { sprintf(msg,"%sBSR\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[2]=true; CPU[ID].flag[4]=true;
     if (IMM > 0) { CPU[ID].REGs[C] = CPU[ID].REGs[A]>>IMM;             if(CPU[ID].REGs[A]>>IMM            <0){CPU[ID].flag[2]=false;}}
     else {         CPU[ID].REGs[C] = CPU[ID].REGs[A]>>CPU[ID].REGs[B]; if(CPU[ID].REGs[A]>>CPU[ID].REGs[B]<0){CPU[ID].flag[2]=false;}} break;
    case 0x0B:// NOT    |
     if (sys.Debug == true) { sprintf(msg,"%sNOT\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     CPU[ID].REGs[A] = ~CPU[ID].REGs[A]; break;
    case 0x0C:// flag  |
     if (sys.Debug == true) { sprintf(msg,"%sFLAG\n",msg); } CPU[ID].REGs[A] = 0;
     for(i=0;i<8;i++) {CPU[ID].REGs[A]+=pow(2,i)*CPU[ID].flag[i]; if(sys.Debug==true){printf("CPU[ID].flag[%d]: %s\n",i,(CPU[ID].flag[i])?"true":"false");} CPU[ID].flag[i]=false;} CPU[ID].flag[0]=true; break;
    case 0x0D:// JMP    |
     if (sys.Debug == true) { sprintf(msg,"%sJUMP\n",msg); }
     memset(CPU[ID].flag, 0, 8);
     if (C >= 1) { CPU[ID].IP = (CPU[ID].REGs[A]<<16|CPU[ID].REGs[B])-6; CPU[ID].flag[1]=true; }else{ CPU[ID].IP = IMM-6; } break;
    case 0x0E:// CMPEQ  |
     if (sys.Debug == true) { sprintf(msg,"%sCMP=\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM>0) {  if (CPU[ID].REGs[A] != IMM) { CPU[ID].IP += ((C>0)?C:1)*6; }
     }else if(CPU[ID].REGs[A] != CPU[ID].REGs[B]) { CPU[ID].IP += ((C>0)?C:1)*6; } break;
    case 0x0F:// CMPLT  |
     if (sys.Debug == true) { sprintf(msg,"%sCMP<\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM>0) { if (!(CPU[ID].REGs[A] <  IMM)) { CPU[ID].IP += ((C>0)?C:1)*6; }
     }else if(!(CPU[ID].REGs[A] <  CPU[ID].REGs[B])) { CPU[ID].IP += ((C>0)?C:1)*6; } break;
    case 0x10:// CMPGT  |
     if (sys.Debug == true) { sprintf(msg,"%sCMP>\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true; CPU[ID].flag[4]=true;
     if (IMM>0) {  if (~(CPU[ID].REGs[A] >  IMM)) { CPU[ID].IP += ((C>0)?C:1)*6; }
     }else if(~(CPU[ID].REGs[A] >  CPU[ID].REGs[B])) { CPU[ID].IP += ((C>0)?C:1)*6; } break;
    case 0x11:// SPLIT  |
     if (sys.Debug == true) { sprintf(msg,"%sSPLIT\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true;
     if ((IMM % 0x2) == 0) { CPU[ID].REGs[B] = CPU[ID].REGs[A] & 0xFF; CPU[ID].REGs[C] = CPU[ID].REGs[A] >> 8; } else { CPU[ID].REGs[B] = CPU[ID].REGs[A] & 0xF; CPU[ID].REGs[C] = CPU[ID].REGs[A] >> 4; } break;
    case 0x12:// COMB   |
     if (sys.Debug == true) { sprintf(msg,"%sCOMBINE\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true; CPU[ID].flag[1]=true;
     if ((IMM % 0x2) == 0) { CPU[ID].REGs[C] = (CPU[ID].REGs[A] << 8) | (CPU[ID].REGs[B] & 0xFF); }else{ CPU[ID].REGs[C] = (CPU[ID].REGs[A] << 4) | (CPU[ID].REGs[B] & 0xF); } break;

    case 0x13:// WMEM   |
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true;
     if (IMM > 0xD7FFFFF) { IMM = (CPU[ID].REGs[B]<<16|CPU[ID].REGs[C])%0xD7FFFFF; }
     if (IMM > 0x0FFFFFF) {
      if (sys.Debug == true) {
       sprintf(msg,"%sWMEM\n  \\Writing REG:%c to 0x%x  (Area ",msg,sys.REG[A],IMM);
       if      (IMM>=0x0000000 && IMM<=0x07FFFFF) { sprintf(msg,"%s0:ROM PAGE#0)\n",msg); }
       else if (IMM>=0x0800000 && IMM<=0x0FFFFFF) { sprintf(msg,"%s1:ROM PAGE#1)\n",msg); }
       else if (IMM>=0x1000000 && IMM<=0x17FFFFF) { sprintf(msg,"%s2:SAV data)\n",msg); }
       else if (IMM>=0x1800000 && IMM<=0x96FFBFF) { sprintf(msg,"%s3:Work RAM)\n",msg); }
       else if (IMM>=0x96FFC00 && IMM<=0x97FFBFF) { sprintf(msg,"%s4:Stack Memory)\n",msg); }
       else if (IMM>=0x97FFC00 && IMM<=0x97FFFFF) { sprintf(msg,"%s5:Static Memory)\n",msg); }
       else if (IMM>=0x9800000 && IMM<=0xD77FFFF) { sprintf(msg,"%s6:Video RAM)\n",msg); }
       else if (IMM>=0xD780000 && IMM<=0xD7FFFFF) { sprintf(msg,"%s7:Stack VMem)\n",msg); }
       else { sprintf(msg,"%sInvalid Address)\n",msg); }
      } sys.MEM[IMM]=CPU[ID].REGs[A]; if (IMM>=0x1000000 && IMM<=0x17FFFFF) { WriteSAV(IMM-0x1000000, CPU[ID].REGs[A]); }
     } else {
      sprintf(msg,"%s[EMU Warning] CPU#%i: Invalid Instuction, You cannot write to ROM!\n",msg,ID);
     } break;
    case 0x14:// RMEM   |
     if (IMM > 0xD7FFFFF) { IMM = (CPU[ID].REGs[B]<<16|CPU[ID].REGs[C])%0xD7FFFFF; }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true;
     if (sys.Debug == true) {
      sprintf(msg,"%sRMEM\n  \\EMU Service: Reading 0x%x to REG:%c (Area ",msg,IMM,sys.REG[A]);
      if      (IMM>=0x0000000 && IMM<=0x07FFFFF) { sprintf(msg,"%s0:ROM PAGE#0)\n",msg); }
      else if (IMM>=0x0800000 && IMM<=0x0FFFFFF) { sprintf(msg,"%s1:ROM PAGE#1)\n",msg); }
      else if (IMM>=0x1000000 && IMM<=0x17FFFFF) { sprintf(msg,"%s2:SAV data)\n",msg); }
      else if (IMM>=0x1800000 && IMM<=0x96FFBFF) { sprintf(msg,"%s3:Work RAM)\n",msg); }
      else if (IMM>=0x96FFC00 && IMM<=0x97FFBFF) { sprintf(msg,"%s4:Stack Memory)\n",msg); }
      else if (IMM>=0x97FFC00 && IMM<=0x97FFFFF) { sprintf(msg,"%s5:Static Memory)\n",msg); }
      else if (IMM>=0x9800000 && IMM<=0xD77FFFF) { sprintf(msg,"%s6:Video RAM)\n",msg); }
      else if (IMM>=0xD780000 && IMM<=0xD7FFFFF) { sprintf(msg,"%s7:Stack VMem)\n",msg); }
      else { sprintf(msg,"%sInvalid Address)\n",msg); }
     } CPU[ID].REGs[A]=sys.MEM[IMM]; break;

    case 0x15:// HALT   |
     if (sys.Debug == true) { sprintf(msg,"%sHALT\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true;
     sys.ErrorType = 0;
     sprintf(sys.Error,"HALT INSTUCTION NOT FINISHED!");
     for(i=0;i<2;i++) {CPU[i].running=false; sprintf(msg,"%sHALTING CPU#%i!\n",msg,i);}
     break;
    case 0x16:// DISP   |
     if (sys.Debug == true) { sprintf(msg,"%sDISP\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true;
     if (!sys.blockDisp) {
      if((IMM%3)==0) { printf("%c: 0x%04X\n",sys.REG[A],CPU[ID].REGs[A]); } else
      if((IMM%3)==1) { printf("%c: 0x%04X\t%c: 0x%04X\t\n",sys.REG[A],CPU[ID].REGs[A],sys.REG[B],CPU[ID].REGs[B]); } else
      if((IMM%3)==2) { printf("%c: 0x%04X\t%c: 0x%04X\t%c: 0x%04X\t\n",sys.REG[A],CPU[ID].REGs[A],sys.REG[B],CPU[ID].REGs[B],sys.REG[C],CPU[ID].REGs[C]); }
     } break;
    case 0x17:// IPOUT  |
     if (sys.Debug == true) { sprintf(msg,"%sIPOUT\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true;
     CPU[ID].REGs[A]=CPU[ID].IP>>16; CPU[ID].REGs[B]=CPU[ID].IP&0xFFFF;
     break;
    case 0x18:// PAGE   |
     if (sys.Debug == true) { sprintf(msg,"%sPAGE\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true;
     LoadPage(A%2,IMM&0xFF%0x21);
     break;
    case 0x19:// CORE   |
     if (sys.Debug == true) { sprintf(msg,"%sCORE\n",msg); }
     //A = CoreID
     //B = State
     //IMM = Start Address
     CPU[A%2].IP = IMM;
     CPU[A%2].running = B%2;
     break;
    case 0x1A:// PUSH   |
     if (sys.Debug == true) { sprintf(msg,"%sPUSH\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true;
     if (CPU[ID].SP-2 < CPU[ID].BP) { sys.ErrorType = 3; sprintf(sys.Error,"CPU#%i: PANIC! STACK OVERFLOW!!",ID); CPU[ID].running=false; break;}
     else {sys.MEM[CPU[ID].SP--] = CPU[ID].REGs[A]&0xFF,sys.MEM[CPU[ID].SP--] = CPU[ID].REGs[A]>>8; } break;
    case 0x1B:// POP    |
     if (sys.Debug == true) { sprintf(msg,"%sPOP\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[0]=true;
     if (CPU[ID].SP+2 > CPU[ID].MP) { sys.ErrorType = 3; sprintf(sys.Error,"CPU#%i: PANIC! stack empty...",ID); CPU[ID].running=false; break;}
     else {CPU[ID].REGs[A] = sys.MEM[CPU[ID].SP++]|(sys.MEM[CPU[ID].SP++]<<8); } break;
    case 0x1C:// CALL   |
     if (sys.Debug == true) { sprintf(msg,"%sCALL\n",msg); } if(C>0){CPU[ID].flag[1]=true;}
     if (CPU[ID].SP-4 < CPU[ID].BP) { sys.ErrorType = 3; sprintf(sys.Error,"CPU#%i: PANIC! STACK OVERFLOW!!",ID); CPU[ID].running=false; break;}
     else {sys.MEM[CPU[ID].SP--] = CPU[ID].IP,sys.MEM[CPU[ID].SP--] = CPU[ID].IP>>8,sys.MEM[CPU[ID].SP--] = CPU[ID].IP>>16,sys.MEM[CPU[ID].SP--] = CPU[ID].IP>>24,CPU[ID].IP = ((C==0)?IMM:(A<<16|B))&0xFFFFFFFFF;} break;
    case 0x1D:// RET    |
     if (sys.Debug == true) { sprintf(msg,"%sRET\n",msg); }
     memset(CPU[ID].flag, 0, 8);
     if (CPU[ID].SP+4 > CPU[ID].MP) {sys.ErrorType = 3; sprintf(sys.Error,"CPU#%i: PANIC! stack empty...",ID); CPU[ID].running=false; break;}
      CPU[ID].IP = sys.MEM[CPU[ID].SP--]|sys.MEM[CPU[ID].SP--]<<8|sys.MEM[CPU[ID].SP--]<<16|sys.MEM[CPU[ID].SP--]<<24; break;
    case 0x1E:// SWAP   |
     if (sys.Debug == true) { sprintf(msg,"%sSWAP\n",msg); }
     memset(CPU[ID].flag, 0, 8);
     if (!CPU[ID].SP+4 > CPU[ID].MP) {
      sys.tmp[0]=sys.MEM[CPU[ID].SP+3],sys.tmp[1]=sys.MEM[CPU[ID].SP+4];
      sys.MEM[CPU[ID].SP+3]=sys.MEM[CPU[ID].SP+1],sys.MEM[CPU[ID].SP+4]=sys.MEM[CPU[ID].SP+2];
      sys.MEM[CPU[ID].SP+1]=sys.tmp[0],sys.MEM[CPU[ID].SP+2]=sys.tmp[1];
     } break;
    case 0x1F:// LED    |
     if (sys.Debug == true) { sprintf(msg,"%sLED\n",msg); }
     memset(CPU[ID].flag, 0, 8); if(C>0){CPU[ID].flag[1]=true;}
     //TODO
     break;
    case 0x20:// CLK    |
     if (sys.Debug == true) { sprintf(msg,"%sCLOCK\n",msg); }
     memset(CPU[ID].flag, 0, 8);
     if(!(IMM>0)) { CPU[ID].flag[1]=true; CPU[ID].REGs[A]=sys.Clock; }
     else { sys.Clock=0; } break;
    case 0x21:// WAIT   |
     if (sys.Debug == true) { sprintf(msg,"%sWAIT\n",msg); }
     memset(CPU[ID].flag, 0, 8); CPU[ID].flag[1]=true;
     dslp = CPU[ID].REGs[A];
     break;
    case 0xFF:// NOP    |
     if (sys.Debug == true) { sprintf(msg,"%sNOP\n",msg); }
     memset(CPU[ID].flag, 0, 8); break;
    default:
     if (sys.Debug == true) { sprintf(msg,"%sUNKNOWN\n",msg); }
     sys.ErrorType = 2;
     sprintf(sys.Error,"CPU#%i: Unknown Operation 0x%02X",ID,sys.MEM[CPU[ID].IP]);
     CPU[ID].running = 0;
      break;
   } CPU[ID].IP+=6; CPU[ID].IPS++; CPU[ID].TI++; sys.TI++;
   if (sys.Debug == true) { printf("%s",msg); memset(msg,0,sizeof(msg)); }
   if (sys.AsService == true) {sendError(); } else { printError(); }
   if (sys.MEM[CPU[ID].IP] == 0x21) { usleep(dslp*1000); }
   else {
    //what do i do here? clearly this isn't right (too slow)
    //usleep((1-(CPU[ID].IPS/24000000.0f))*1000);
    
    // this only gives around 50%
    for(uint32_t i=0;i<CPU[ID].IPS/ 6000000;i++);
   }
//   sleep(1);
//   while(getchar()!='\n');
//   dumpData("ROMPG#1", sys.MEM, SIZ8MB, SIZ8MB, SIZ8MB+0x40);
}}}

