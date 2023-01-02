
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>

#include "pes.h"

#ifdef WIN32
  typedef int ssize_t;
#endif

extern uint64_t audio_pts,first_audio_pts;
extern int audio_pts_wrap;
extern uint16_t apid;

ssize_t safe_read(int fd, unsigned char* buf, size_t count) {
  ssize_t n=1, t=0;

  while (n>0 && count >0) {
    n=read(fd,buf+t,count);
    count-=n;
    t+=n;
  }
  return t;
}

char pts_text[30];
char* pts2hmsu(uint64_t pts,char sep) {
  int h,m,s,u;

  pts/=90; // Convert to milliseconds
  h=(pts/(1000*60*60));
  m=(pts/(1000*60))-(h*60);
  s=(pts/1000)-(h*3600)-(m*60);
  u=pts-(h*1000*60*60)-(m*1000*60)-(s*1000);

  sprintf(pts_text,"%d:%02d:%02d%c%03d",h,m,s,sep,u);
  return(pts_text);
}

uint64_t get_pes_pts (unsigned char* buf) {
  uint64_t PTS;
  int PTS_DTS_flags;
  uint64_t p0,p1,p2,p3,p4;

  PTS_DTS_flags=(buf[7]&0xb0)>>6;
  if ((PTS_DTS_flags&0x02)==0x02) {
    // PTS is in bytes 9,10,11,12,13
    p0=(buf[13]&0xfe)>>1|((buf[12]&1)<<7);
    p1=(buf[12]&0xfe)>>1|((buf[11]&2)<<6);
    p2=(buf[11]&0xfc)>>2|((buf[10]&3)<<6);
    p3=(buf[10]&0xfc)>>2|((buf[9]&6)<<5);
    p4=(buf[9]&0x08)>>3;

    PTS=p0|(p1<<8)|(p2<<16)|(p3<<24)|(p4<<32);
  } else {
    PTS=0;
  }
  return(PTS);
}

int read_pes_packet (int fd, uint16_t pid, uint8_t* buf, int vdrmode) {
  int i;
  int n,stream_id;
  int count;
  int PES_packet_length;
  uint16_t packet_pid;
  uint8_t tsbuf[188];
  int adaption_field_control,discontinuity_indicator,adaption_field_length;
  int synced=0;
  int finished=0;
  int ts_payload;
  uint64_t tmp_pts;

  if (vdrmode) {
    while (!finished) {
      count=safe_read(fd,buf,3);
      if ((buf[0]==0x00) && (buf[1]==0x00) && (buf[2]==0x01)) {
        count=safe_read(fd,&buf[3],3);
        if (count!=3) return(-1);
        stream_id=buf[3];
        PES_packet_length=(buf[4]<<8)|buf[5];
        count=safe_read(fd,&buf[6],PES_packet_length);
        if (count!=PES_packet_length) return(-1);

        if (stream_id==0xbd) {
          finished=1;
        } else if (stream_id==0xc0) {
          tmp_pts=get_pes_pts(buf);
          if (tmp_pts != 0) {
            if ((audio_pts > (uint64_t)0x1ffff0000LL) && (tmp_pts < (uint64_t)0x000100000LL)) {
              audio_pts_wrap=1;
            }
            if (audio_pts_wrap) { tmp_pts+=0x200000000LL; }
          }
          if (tmp_pts > audio_pts) { audio_pts=tmp_pts; }
          if ((first_audio_pts==0) || ((tmp_pts!=0) && (tmp_pts < first_audio_pts))) { first_audio_pts=tmp_pts; }
        }
      }
    }
  } else {
    n=0; // Bytes copied into buf.
  
    memset(buf,0xff,sizeof(buf));
    while (!finished) {
      count=safe_read(fd,tsbuf,188);
     if (count!=188) return(-1);
  
      if (tsbuf[0]!=0x47) { 
        fprintf(stderr,"ERROR: TS sync byte not present, aborting.\n");
        return(0);
      }
  
      packet_pid=(((tsbuf[1]&0x1f)<<8) | tsbuf[2]);
  
      adaption_field_control=(tsbuf[3]&0x30)>>4;
      discontinuity_indicator=0;
      if (adaption_field_control==3) {
        adaption_field_length=tsbuf[4]+1;
      } else if (adaption_field_control==2) {
        adaption_field_length=183+1;
      } else {
        adaption_field_length=0;
      }
      i=4+adaption_field_length;
      ts_payload=184-adaption_field_length;
  
      if (packet_pid==pid) {
  //      fprintf(stderr,"Read %d bytes from pid %d, synced=%d\n",count,pid,synced); 
        if (!synced) {
          if (tsbuf[1]&0x40) {
            if ((tsbuf[i]==0x00) && (tsbuf[i+1]==0x00) && (tsbuf[i+2]==0x01)) {
              synced=1;
              stream_id=tsbuf[i+3];
              if (stream_id!=0xbd) {
                fprintf(stderr,"ERROR: PID %d does not contain a Private Data stream.  Aborting.\n",pid);
              }
              PES_packet_length=(tsbuf[i+4]<<8)|tsbuf[i+5];
              memcpy(buf,&tsbuf[i],ts_payload);
              n=ts_payload;
            }
          }
        } else {
          memcpy(&buf[n],&tsbuf[i],ts_payload);
          n+=ts_payload;
        }
        if ((synced) && (n >= PES_packet_length)) { finished=1; }
      } else {
        if ((tsbuf[1]&0x40) && (tsbuf[i]==0x00) && (tsbuf[i+1]==0x00) && (tsbuf[i+2]==0x01)) {
          stream_id=tsbuf[i+3];

          if (stream_id==0xc0) {
            if (apid==0) { 
              apid=packet_pid;
              fprintf(stderr,"INFO: Found audio stream %d\n",apid);
            }
            if (apid==packet_pid) {
              tmp_pts=get_pes_pts(buf);
              if (tmp_pts != 0) {
                if ((audio_pts > (uint64_t)0x1ffff0000LL) && (tmp_pts < (uint64_t)0x000100000LL)) {
                  audio_pts_wrap=1;
                }
                if (audio_pts_wrap) { tmp_pts+=0x200000000LL; }
              }
              if (tmp_pts > audio_pts) { audio_pts=tmp_pts; }
              if ((first_audio_pts==0) || ((tmp_pts!=0) && (tmp_pts < first_audio_pts))) { first_audio_pts=tmp_pts; }
            }
          }
        }
      }
    }
  }
  return(PES_packet_length);
}

