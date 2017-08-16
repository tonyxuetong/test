#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "librtmp/rtmp_sys.h"
#include "librtmp/log.h"
/*
build in unix
gcc -Wall -o sendflvrtmp  sendflvrtmp.c -lpthread -Llibrtmp -lrtmp -lssl -lcrypto -lz
small head
*/

#ifdef WIN32
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"lib/librtmp.lib")
#endif


#define HTON16(x)  ((x>>8&0xff)|(x<<8&0xff00))
#define HTON24(x)  ((x>>16&0xff)|(x<<16&0xff0000)|(x&0xff00))
#define HTON32(x)  ((x>>24&0xff)|(x>>8&0xff00)|\
(x<<8&0xff0000)|(x<<24&0xff000000))
#define HTONTIME(x) ((x>>16&0xff)|(x<<16&0xff0000)|(x&0xff00)|(x&0xff000000))

int ReadU8(uint32_t *u8,FILE*fp);
int ReadU16(uint32_t *u16,FILE*fp);
int ReadU24(uint32_t *u24,FILE*fp);
int ReadU32(uint32_t *u32,FILE*fp);
int PeekU8(uint32_t *u8,FILE*fp);
int ReadTime(uint32_t *utime,FILE*fp);

//RTMP_XXX()返回0表示失败，1表示成功

RTMP*rtmp=NULL;//rtmp应用指针
RTMPPacket*packet=NULL;//rtmp包结构
char* rtmpurl="rtmp://47.93.99.227:1935/myapp/test";//连接的URL
char* flvfilename="test.mp4";//读取的flv文件



int ZINIT();//初始化相关
void ZCLEAR();//清除相关

int main(){
	long start=0;
	long perframetime=0;
	RTMP_LogLevel lvl=RTMP_LOGINFO;
	FILE*fp=NULL;	
	if (!ZINIT()){
		printf("Init Socket Err\n");
		return -1;
	}
/////////////////////////////////初始化//////////////////////	
//	RTMP_debuglevel=RTMP_LOGINFO;//信息等级(0-6)
	RTMP_LogSetLevel(lvl);//设置信息等级
//	RTMP_LogSetOutput(FILE*fp);//设置信息输出文件

	rtmp=RTMP_Alloc();//申请rtmp空间
	RTMP_Init(rtmp);//初始化rtmp设置
	rtmp->Link.timeout=5;//设置连接超时，单位秒，默认30秒
	packet=(RTMPPacket*)malloc(sizeof(RTMPPacket));//创建包
	memset(packet,0,sizeof(RTMPPacket));
	RTMPPacket_Alloc(packet,1024*64);//给packet分配数据空间
	RTMPPacket_Reset(packet);//重置packet状态
////////////////////////////////连接//////////////////
	RTMP_SetupURL(rtmp,rtmpurl);//设置url
	RTMP_EnableWrite(rtmp);//设置可写状态
	//连接服务器
	if (!RTMP_Connect(rtmp,NULL)){
		printf("Connect Err\n");
		ZCLEAR();
		return -1;
	}
	//创建并发布流(取决于rtmp->Link.lFlags)
	if (!RTMP_ConnectStream(rtmp,0)){
		printf("ConnectStream Err\n");
		ZCLEAR();
		return -1;
	}
	packet->m_hasAbsTimestamp = 0; //绝对时间戳
	packet->m_nChannel = 0x04; //通道
	packet->m_nInfoField2 = rtmp->m_stream_id;

	fp=fopen(flvfilename,"rb");
	if (fp==NULL){
		printf("Open File:%s Err\n",flvfilename);
		ZCLEAR();
		return -1;
	}

	printf("rtmpurl:%s\nflvfile:%s\nsend data ...\n",rtmpurl,flvfilename);
////////////////////////////////////////发送数据//////////////////////
	fseek(fp,9,SEEK_SET);//跳过前9个字节
	fseek(fp,4,SEEK_CUR);//跳过4字节长度
	start=time(NULL);
	perframetime=0;//上一帧时间戳
	while(TRUE){
		uint32_t type=0;//类型
		uint32_t datalength=0;//数据长度
		static uint32_t timestamp=0;//时间戳
		uint32_t streamid=0;//流ID
		uint32_t alldatalength=0;//该帧总长度
	//	uint32_t readlength=0;//读取长度
	//	printf("Start:%u Perf:%u Clock:%u\n",start,perframetime,clock()/1000);

		if((time(NULL)-start)<(perframetime/1000)){	
			//发的太快就等一下
#ifdef WIN32
			printf("\rTimeStamp:%8u ms",timestamp);
			Sleep(1000);
#else
			printf("TimeStamp:%8u ms\n",timestamp);
			sleep(1);
#endif
			continue;
		}	
		if(!ReadU8(&type,fp))
			break;
		if(!ReadU24(&datalength,fp))
			break;
		if(!ReadTime(&timestamp,fp))
			break;
		if(!ReadU24(&streamid,fp))
			break;
		if (type!=0x08&&type!=0x09){
			//跳过非音视频桢
			fseek(fp,datalength+4,SEEK_CUR);
			continue;
		}

		if(fread(packet->m_body,1,datalength,fp)!=datalength)
			break;
		packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM; 
		packet->m_nTimeStamp = timestamp; 
		packet->m_packetType=type;
		packet->m_nBodySize=datalength;

		if (!RTMP_IsConnected(rtmp)){
			printf("rtmp is not connect\n");
			break;
		}
		if (!RTMP_SendPacket(rtmp,packet,0)){
			printf("Send Err\n");
			break;
		}
		if(!ReadU32(&alldatalength,fp))
			break;
		perframetime=timestamp;
	}
	printf("\nSend Data Over\n");
	fclose(fp);
	ZCLEAR();
	return 0;
}

int ZINIT(){
#ifdef WIN32
	WORD version;
	WSADATA wsaData;
	version=MAKEWORD(2,2);
	if(WSAStartup(version,&wsaData)!=0){
		return 0;
	}
#endif
	return 1;
}
void ZCLEAR(){
	//////////////////////////////////////////释放/////////////////////
	if (rtmp!=NULL){
		RTMP_Close(rtmp);//断开连接
		RTMP_Free(rtmp);//释放内存
		rtmp=NULL;
	}
	if (packet!=NULL){
		RTMPPacket_Free(packet);//释放内存
		free(packet);
		packet=NULL;
	}
	///////////////////////////////////////////////////
#ifdef WIN32
	WSACleanup();
#endif
}

int ReadU8(uint32_t *u8,FILE*fp);
int ReadU16(uint32_t *u16,FILE*fp);
int ReadU24(uint32_t *u24,FILE*fp);
int ReadU32(uint32_t *u32,FILE*fp);
int PeekU8(uint32_t *u8,FILE*fp);
int ReadTime(uint32_t *utime,FILE*fp);


int ReadU8(uint32_t *u8,FILE*fp){
	if(fread(u8,1,1,fp)!=1)
		return 0;
	return 1;
}
int ReadU16(uint32_t *u16,FILE*fp){
	if(fread(u16,2,1,fp)!=1)
		return 0;
	*u16=HTON16(*u16);
	return 1;
}
int ReadU24(uint32_t *u24,FILE*fp){
	if(fread(u24,3,1,fp)!=1)
		return 0;
	*u24=HTON24(*u24);
	return 1;
}
int ReadU32(uint32_t *u32,FILE*fp){
	if(fread(u32,4,1,fp)!=1)
		return 0;
	*u32=HTON32(*u32);
	return 1;
}
int PeekU8(uint32_t *u8,FILE*fp){
	if(fread(u8,1,1,fp)!=1)
		return 0;
	fseek(fp,-1,SEEK_CUR);
	return 1;
}
int ReadTime(uint32_t *utime,FILE*fp){
	if(fread(utime,4,1,fp)!=1)
		return 0;
	*utime=HTONTIME(*utime);
	return 1;
}
