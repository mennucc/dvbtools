/*

   dvbtune - a program for tuning DVB TV and Radio channels.

   Initial transponders for "-x" option:

   Astra   28E:  
   Astra   19E: 12670v - srate 22000
   Hotbird 13E: 10911v - srate 27500  ?? Doesn't work!
   Thor etc 1W: 11247v - srate 24500 (Most channels!)

   Copyright (C) Dave Chapman 2001 
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html

   Added Switch -n that adds a network interface and switch -m that monitors
   the reception quality. Changed the tuning code
   Added command line parameters for spectral inversion. Changed code to allow
   L-Band frequencies with -f switch

   Copyright (C) Hilmar Linder 2002

   
*/

// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>

// DVB includes:
#include <ost/osd.h>
#include <ost/dmx.h>
#include <ost/sec.h>
#include <ost/frontend.h>
#include <ost/video.h>
#include <ost/audio.h>
#include <ost/net.h>

#include "tune.h"

#define SECA_CA_SYSTEM	0x100
#define VIACCESS_1_CA_SYSTEM	0x500
#define VIACCESS_2_CA_SYSTEM	0x50000
#define IRDETO_CA_SYSTEM	0x602
#define BETA_CA_SYSTEM		0x1702

int fd_demuxv,fd_demuxa,fd_demuxtt,fd_demuxsi,fd_demuxrec,fd_demuxd;
int pnr=-1;
int apid=0;
int vpid=0;
int card=0;
SpectralInversion specInv = INVERSION_AUTO;
int tone = -1;

char* frontenddev[4]={"/dev/ost/frontend0","/dev/ost/frontend1","/dev/ost/frontend2","/dev/ost/frontend3"};
char* dvrdev[4]={"/dev/ost/dvr0","/dev/ost/dvr1","/dev/ost/dvr2","/dev/ost/dvr3"};
char* secdev[4]={"/dev/ost/sec0","/dev/ost/sec1","/dev/ost/sec2","/dev/ost/sec3"};
char* demuxdev[4]={"/dev/ost/demux0","/dev/ost/demux1","/dev/ost/demux2","/dev/ost/demux3"};

typedef struct _transponder_t {
  int id;
  int onid;
  unsigned int freq;
  int srate;
  int pos;
  int we_flag;
  char pol;
  int mod;

  int scanned;
  struct _transponder_t* next;
} transponder_t;

transponder_t* transponders=NULL;
int num_trans=0;

transponder_t transponder;

typedef struct _pat_t {
  int service_id;
  int pmt_pid;
  int scanned;
  struct _pat_t* next;
} pat_t;

pat_t* pats=NULL;

/* Get the first unscanned transponder (or return NULL) */
transponder_t*  get_unscanned() {
  transponder_t* t;
  
  t=transponders;

  while (t!=NULL) {
    if (t->scanned==0) { return(t); };
    t=t->next;
  }
  return NULL;
}

char xmlify_result[5];
char* xmlify (char c) {
  switch(c) {
    case '&': strcpy(xmlify_result,"&amp;");
              break;
    case '<': strcpy(xmlify_result,"&lt;");
              break;
    case '>': strcpy(xmlify_result,"&gt;");
              break;
    case '\"': strcpy(xmlify_result,"&quot;");
              break;
    case 0: xmlify_result[0]=0;
              break;
    default: xmlify_result[0]=c;
             xmlify_result[1]=0;
             break;
 }
 return(xmlify_result);
}

void add_transponder(transponder_t transponder) {
  transponder_t* t;
  int found;

  if (transponders==NULL) {
    transponders=(transponder_t*)malloc(sizeof(transponder));

    transponders->freq=transponder.freq;
    transponders->srate=transponder.srate;
    transponders->pol=transponder.pol;
    transponders->pos=transponder.pos;
    transponders->we_flag=transponder.we_flag;
    transponders->mod=transponder.mod;
    transponders->scanned=0;
    transponders->next=NULL;
  } else {
    t=transponders;
    found=0;
    while ((!found) && (t!=NULL)) {
       /* Some transponders appear with slightly different frequencies -
          ignore a new transponder if it is within 3MHz of another */
       if ((abs(t->freq-transponder.freq)<=3000) && (t->pol==transponder.pol)) {
          found=1;
       } else {
         t=t->next;
       }
    }

    if (!found) {
      t=(transponder_t*)malloc(sizeof(transponder));

      t->freq=transponder.freq;
      t->srate=transponder.srate;
      t->pol=transponder.pol;
      t->pos=transponder.pos;
      t->we_flag=transponder.we_flag;
      t->mod=transponder.mod;
      t->scanned=0;
      t->next=transponders;

      transponders=t;
    }
  }
}

void free_pat_list() {
  pat_t* t=pats;

  while (pats!=NULL) {
    t=pats->next;
    free(pats);
    pats=t;
  }
}

int get_pmt_pid(int service_id) {
  pat_t* t=pats;
  int found=0;

  while ((!found) && (t!=NULL)) {
    if (t->service_id==service_id) {
      found=1;
    } else {
      t=t->next;
    }
  }

  if (found) {
    return(t->pmt_pid);
  } else {
    return(0);
  }
}

void add_pat(pat_t pat) {
  pat_t* t;
  int found;

  if (pats==NULL) {
    pats=(pat_t*)malloc(sizeof(pat));

    pats->service_id=pat.service_id;
    pats->pmt_pid=pat.pmt_pid;
    pats->scanned=0;
    pats->next=NULL;
  } else {
    t=pats;
    found=0;
    while ((!found) && (t!=NULL)) {
       if ((t->service_id==pat.service_id)) {
          found=1;
       } else {
         t=t->next;
       }
    }

    if (!found) {
      t=(pat_t*)malloc(sizeof(pat));

      t->service_id=pat.service_id;
      t->pmt_pid=pat.pmt_pid;
      t->scanned=0;
      t->next=pats;

      pats=t;
    }
  }
}

void set_recpid(int fd, ushort ttpid) 
{  
struct dmxPesFilterParams pesFilterParamsREC;

        if (ttpid==0 || ttpid==0xffff) {
	        ioctl(fd, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsREC.pid     = ttpid;
	pesFilterParamsREC.input   = DMX_IN_FRONTEND; 
	pesFilterParamsREC.output  = DMX_OUT_TAP; 
	pesFilterParamsREC.pesType = DMX_PES_OTHER; 
	pesFilterParamsREC.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd, DMX_SET_PES_FILTER, 
		  &pesFilterParamsREC) < 0)
		perror("set_recpid");
}

void set_sipid(ushort ttpid) 
{  
struct dmxPesFilterParams pesFilterParamsSI;

        if (ttpid==0 || ttpid==0xffff) {
	        ioctl(fd_demuxsi, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsSI.pid     = ttpid;
	pesFilterParamsSI.input   = DMX_IN_FRONTEND; 
	pesFilterParamsSI.output  = DMX_OUT_TS_TAP; 
	pesFilterParamsSI.pesType = DMX_PES_OTHER; 
	pesFilterParamsSI.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxsi, DMX_SET_PES_FILTER, 
		  &pesFilterParamsSI) < 0)
		perror("set_sipid");
}

void set_ttpid(ushort ttpid) 
{  
struct dmxPesFilterParams pesFilterParamsTT;

        if (ttpid==0 || ttpid==0xffff) {
	        ioctl(fd_demuxtt, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsTT.pid     = ttpid;
	pesFilterParamsTT.input   = DMX_IN_FRONTEND; 
	pesFilterParamsTT.output  = DMX_OUT_DECODER; 
	pesFilterParamsTT.pesType = DMX_PES_TELETEXT; 
	pesFilterParamsTT.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxtt, DMX_SET_PES_FILTER, 
		  &pesFilterParamsTT) < 0)
		perror("set_ttpid");
}

void set_vpid(ushort vpid) 
{  
struct dmxPesFilterParams pesFilterParamsV;
        if (vpid==0 || vpid==0xffff) {
	        ioctl(fd_demuxv, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsV.pid     = vpid;
	pesFilterParamsV.input   = DMX_IN_FRONTEND; 
	pesFilterParamsV.output  = DMX_OUT_DECODER; 
	pesFilterParamsV.pesType = DMX_PES_VIDEO; 
	pesFilterParamsV.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxv, DMX_SET_PES_FILTER, 
		  &pesFilterParamsV) < 0)
		perror("set_vpid");
}

void set_apid(ushort apid) 
{  
struct dmxPesFilterParams pesFilterParamsA;
        if (apid==0 || apid==0xffff) {
	        ioctl(fd_demuxa, DMX_STOP, apid);
	        return;
	}
	pesFilterParamsA.pid = apid;
	pesFilterParamsA.input = DMX_IN_FRONTEND; 
	pesFilterParamsA.output = DMX_OUT_DECODER; 
	pesFilterParamsA.pesType = DMX_PES_AUDIO; 
	pesFilterParamsA.flags = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxa, DMX_SET_PES_FILTER, 
		  &pesFilterParamsA) < 0)
		perror("set_apid");
}

void set_dpid(ushort dpid) 
{ 
	struct dmxSctFilterParams sctFilterParams;
 
        if (dpid==0 || dpid==0xffff) {
                ioctl(fd_demuxd, DMX_STOP, dpid);
                return;
        }
	memset(&sctFilterParams.filter.filter,0,DMX_FILTER_SIZE);
	memset(&sctFilterParams.filter.mask,0,DMX_FILTER_SIZE);
        sctFilterParams.pid = dpid;
	//sctFilterParams.filter.filter[0] = 0x3e;
        //sctFilterParams.filter.mask[0] = 0xff; 
	sctFilterParams.timeout = 0;
        sctFilterParams.flags = DMX_IMMEDIATE_START;
        if (ioctl(fd_demuxd, DMX_SET_FILTER, &sctFilterParams) < 0)
                perror("set_dpid"); 
}


void set_ts_filter(int fd,uint16_t pid)
{
  struct dmxPesFilterParams pesFilterParams;

  pesFilterParams.pid     = pid; 
  pesFilterParams.input   = DMX_IN_FRONTEND;
  pesFilterParams.output  = DMX_OUT_TS_TAP;
  pesFilterParams.pesType = DMX_PES_OTHER;
// A HACK TO DECODE STREAMS ON DVB-S CARD WHILST STREAMING
//  if (pid==255) pesFilterParams.pesType = DMX_PES_VIDEO;
//  if (pid==256) pesFilterParams.pesType = DMX_PES_AUDIO;
  pesFilterParams.flags   = DMX_IMMEDIATE_START;

  if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
    fprintf(stderr,"FILTER %i: ",pid);
    perror("DMX SET PES FILTER");
  }
}


void parse_descriptors(int info_len,unsigned char *buf) {
  int i=0;
  int descriptor_tag,descriptor_length,j,k,pid,id;
  int service_type;
  char tmp[128];
  unsigned int freq, pol, sr;

       while (i < info_len) {
        descriptor_tag=buf[i++];
        descriptor_length=buf[i++];
	//        printf("Found descriptor: 0x%02x - length %02d\n",descriptor_tag,descriptor_length);
        while (descriptor_length > 0) {
          switch(descriptor_tag) {
           case 0x03: // audio_stream_descriptor
             printf("<audio_info tag=\"0x03\" info=\"%02x\" />\n",buf[i]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

           case 0x06: // data_stream_alignmentdescriptor
             printf("<data_stream_alignment tag=\"0x06\" data=\"%02x\" />\n",buf[i]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

           case 0x0a: // iso_639_language_descriptor
             for (j=0;j<((descriptor_length)/4);j++) {
               printf("<iso_639 language=\"");
               if (buf[i]!=0) printf("%c",buf[i]);
               if (buf[i+1]!=0) printf("%c",buf[i+1]);
               if (buf[i+2]!=0) printf("%c",buf[i+2]);
               printf("\" type=\"%d\" />\n",buf[i+3]);
               i+=4;
               descriptor_length-=4;
             }
             break;

           case 0x0b: // system_clock_descriptor
             printf("<system_clock tag=\"0x0b\" data=\"%02x%02x\" />\n",buf[i],buf[i+1]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

           case 0x09: // ca_descriptor
             k=(buf[i]<<8)|buf[i+1];
             printf("<ca_system_descriptor system_id=\"0x%02x%02x\" ",buf[i],buf[i+1]);

             switch(k) {
               case SECA_CA_SYSTEM:
                 for (j=2;j<descriptor_length;j+=15) {
                   pid=(buf[i+j]&0x1f<<8)|buf[i+j+1];
                   id=(buf[i+j+2]<<8) | buf[i+j+3];
                   printf("ecm_pid=\"0x%04x\" ecm_id=\"0x%04x\"/>\n",pid,id);
                 }
                 break;
               case VIACCESS_1_CA_SYSTEM:
                 for (j=2;j<descriptor_length;j+=15) {
                   pid=(buf[i+j]&0x1f<<8)|buf[i+j+1];
                   id=(buf[i+j+2]<<8) | buf[i+j+3];
                   printf("ecm_pid=\"0x%04x\" ecm_id=\"0x%04x\"/>\n",pid,id);
                 }
                 break;
               default:
                 printf(" />\n");
                 break;
             }
             i+=descriptor_length;
             descriptor_length=0;
             break;

           case 0x40: // network_name
//             printf("<network_name tag=\"0x40\">");
             j=descriptor_length;
             while(j > 0) {
//               printf("%c",buf[i++]);
               j--;
             }
             descriptor_length=0;
//             printf("</network_name>\n");
             break;
             
           case 0x41: // service_list
//             printf("<services tag=\"0x41\" n=\"%d\">\n",descriptor_length/3);
             while (descriptor_length > 0) {
//               printf("<service id=\"%d\" type=\"%d\" />\n",(buf[i]<<8)|buf[i+1],buf[i+2]);
               i+=3;
               descriptor_length-=3;
             }
//             printf("</services>\n");
             break;

           case 0x43: // satellite_delivery_system
             freq=(unsigned int)(buf[i]<<24)|(buf[i+1]<<16)|(buf[i+2]<<8)|buf[i+3];
             sprintf(tmp,"%x",freq);
             transponder.freq=atoi(tmp)*10;
             i+=4;
             transponder.pos=(buf[i]<<8)|buf[i+1];
             i+=2;
             transponder.we_flag=(buf[i]&0x80)>>7;
             pol=(buf[i]&0x60)>>5;
             switch(pol) {
                 case 0 : transponder.pol='H'; break;
                 case 1 : transponder.pol='V'; break;
                 case 2 : transponder.pol='L'; break;
                 case 3 : transponder.pol='R'; break;
             }
             transponder.mod=buf[i]&0x1f;
             i++;
             sr=(unsigned int)(buf[i]<<24)|(buf[i+1]<<16)|(buf[i+2]<<8)|(buf[i+3]&0xf0);
             sr=(unsigned int)(sr >> 4);
             sprintf(tmp,"%x",sr);
             transponder.srate=atoi(tmp)*100;
             i+=4;
             descriptor_length=0;
             add_transponder(transponder);
//             printf("<satellite_delivery tag=\"0x43\" freq=\"%05d\" srate=\"%d\" pos=\"%04x\" we_flag=\"%d\" polarity=\"%c\" modulation=\"%d\" />\n",transponder.freq,transponder.srate,transponder.pos,transponder.we_flag,transponder.pol,transponder.mod);
	     break;

           case 0x48: // service_description
             service_type=buf[i++];
             printf("<description tag=\"0x48\" type=\"%d\"",service_type);
             descriptor_length--;
             j=buf[i++];
             descriptor_length-=(j+1);
             printf(" provider_name=\"");;
             while(j > 0) {
               printf("%s",xmlify(buf[i++]));
               j--;
             }
             printf("\" service_name=\"");
             j=buf[i++]; 
             descriptor_length-=(j+1);
             while(j > 0) {
               printf("%s",xmlify(buf[i]));
               i++;
               j--;
             }
             printf("\" />\n");
             break;

           case 0x49: // country_availability:
             printf("<country_availability tag=\"0x49\" type=\"%d\" countries=\" ",(buf[i]&0x80)>>7);
             i++;
             j=descriptor_length-1;
             while (j > 0) { 
               printf("%c",buf[i++]);
               j--;
             }
             printf("\" />\n");
             descriptor_length=0;
             break;

          case 0x4c:
             printf("<time_shifted_copy_of tag=\"0x4c\" service_id=\"%d\" />\n",(buf[i]<<8)|buf[i+1]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

          case 0x52: // stream_identifier_descriptor
             printf("<stream_id id=\"%d\" />\n",buf[i]);
             i+=descriptor_length;
             descriptor_length=0;
             break;
	  
          case 0x53:
             printf("<ca_identifier tag=\"0x53\" length=\"%02x\">\n",descriptor_length);
             for (j=0;j<descriptor_length;j+=2) {
               k=(buf[i+j]<<8)|buf[i+j+1];
               printf("<ca_system_id>%04x</ca_system_id>\n",k);
             }
             i+=descriptor_length;
             descriptor_length=0;
             printf("</ca_identifier>\n");
             break;

          case 0x56:
             printf("<teletext tag=\"0x56\">\n");
             while (j < descriptor_length) {
               printf("<teletext_info lang=\"%s%s%s\" type=\"%d\" page=\"%d%02x\" />\n",xmlify(buf[i]),xmlify(buf[i+1]),xmlify(buf[i+2]),(buf[i+3]&0xf8)>>3,(buf[i+3]&0x07),buf[i+4]);
               i+=5;
               j+=5;
             }
             printf("</teletext>\n");
             descriptor_length=0;
             break;

          case 0x6a:
             printf("<ac3_descriptor tag=\"0x6a\" data=\"");
             for (j=0;j<descriptor_length;j++) printf("%02x",buf[i+j]);
             printf("\" />\n");
             i+=descriptor_length;
             descriptor_length=0;
             break;

          case 0xc5: // canal_satellite_radio_descriptor
	    /* This is guessed from the data */
            printf("<canal_radio tag=\"0x%02x\" id=\"%d\" name=\"",descriptor_tag,buf[i]);
            i++;
            j=0;
            for (j=0;j<descriptor_length;j++) 
              if (buf[i+j]!=0) printf("%c",buf[i+j]);
            printf("\" />\n");
            i+=descriptor_length;
            descriptor_length=0;
            break;

          default:
             printf("<descriptor tag=\"0x%02x\" data=\"",descriptor_tag);
             for (j=0;j<descriptor_length;j++) printf("%02x",buf[i+j]);
             printf("\" text=\"");
             for (j=0;j<descriptor_length;j++) printf("%c",(isalnum(buf[i+j]) ? buf[i+j] : '.'));
             printf("\" />\n");
             i+=descriptor_length;
             descriptor_length=0;
             break;
          }
        }
      }
}

void dump(char* fname, int len, char* buf) {
  FILE* f;

  f=fopen(fname,"w");
  if (f) {
    fwrite(buf,1,len,f);
    fclose(f);
  }
}

int scan_nit(int x) {
  int fd_nit;
  int n,seclen;
  int i;
  struct pollfd ufd;
  unsigned char buf[4096];
  struct dmxSctFilterParams sctFilterParams;
  int info_len,network_id;

  if((fd_nit = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_nit DEVICE: ");
      return -1;
  }

  sctFilterParams.pid=0x10;
  memset(&sctFilterParams.filter.filter,0,DMX_FILTER_SIZE);
  memset(&sctFilterParams.filter.mask,0,DMX_FILTER_SIZE);
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=x;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_nit,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("NIT - DMX_SET_FILTER:");
    close(fd_nit);
    return -1;
  }

  ufd.fd=fd_nit;
  ufd.events=POLLPRI;
  if (poll(&ufd,1,5000) < 0 ) {
    fprintf(stderr,"TIMEOUT on read from fd_nit\n");
    close(fd_nit);
    return -1;
  }
  if (read(fd_nit,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_nit,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
//      dump("nit.dat",seclen,buf);
//      printf("<nit>\n");
      network_id=(buf[3]<<8)|buf[4];
//      printf("<network id=\"%d\">\n",network_id);

      info_len=((buf[8]&0x0f)<<8)|buf[9];
      i=10;
      parse_descriptors(info_len,&buf[i]);
      i+=info_len;
      i+=2;
      while (i < (seclen-4)) {
        transponder.id=(buf[i]<<8)|buf[i+1];
        i+=2;
        transponder.onid=(buf[i]<<8)|buf[i+1];
        i+=2;
	//        printf("<transponder id=\"%d\" onid=\"%d\">\n",transponder.id,transponder.onid);
        info_len=((buf[i]&0x0f)<<8)|buf[i+1];
        i+=2;
        parse_descriptors(info_len,&buf[i]);
//        printf("</transponder>\n");
        i+=info_len;
      }
//      printf("</network>\n");
//      printf("</nit>\n");
    } else {
      fprintf(stderr,"Under-read bytes for NIT - wanted %d, got %d\n",seclen,n);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_nit\n");
  }
  close(fd_nit);
  return(0);
}

void scan_pmt(int pid,int sid,int change) {
  int fd_pmt;
  int n,seclen;
  int i,k;
  int max_k;
  unsigned char buf[4096];
  struct dmxSctFilterParams sctFilterParams;
  int service_id;
  int info_len,es_pid,stream_type;
  struct pollfd ufd;

  //  printf("Scanning pmt: pid=%d, sid=%d\n",pid,sid);

  if (pid==0) { return; }

  if((fd_pmt = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_pmt DEVICE: ");
      return;
  }

  sctFilterParams.pid=pid;
  memset(&sctFilterParams.filter.filter,0,DMX_FILTER_SIZE);
  memset(&sctFilterParams.filter.mask,0,DMX_FILTER_SIZE);
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=0x02;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_pmt,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("PMT - DMX_SET_FILTER:");
    close(fd_pmt);
    return;
  }

  ufd.fd=fd_pmt;
  ufd.events=POLLPRI;
  if (poll(&ufd,1,2000) < 0) {
     fprintf(stderr,"TIMEOUT reading from fd_pmt\n");
     close(fd_pmt);
     return;
  }
  max_k=1;
for (k=0;k<max_k;k++) {
  if (read(fd_pmt,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_pmt,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
//      printf("<pmt>\n");
      service_id=(buf[3]<<8)|buf[4];
//      printf("<service id=\"%d\" pmt_pid=\"%d\">\n",service_id,pid);

      max_k=buf[7]+1; // last_sec_num - read this many (+1) sections
      info_len=((buf[10]&0x0f)<<8)|buf[11];
      i=12;
      parse_descriptors(info_len,&buf[i]);
      i+=info_len;
      while (i < (seclen-4)) {
        stream_type=buf[i++];
        es_pid=((buf[i]&0x1f)<<8)|buf[i+1];
        printf("<stream type=\"%d\" pid=\"%d\">\n",stream_type,es_pid);
        if (change) {
          if ((vpid==0) && ((stream_type==1) || (stream_type==2))) {
             vpid=es_pid;
          }
          if ((apid==0) && ((stream_type==3) || (stream_type==4))) {
            apid=es_pid;
          }
        }

        i+=2;
        info_len=((buf[i]&0x0f)<<8)|buf[i+1];
        i+=2;
        parse_descriptors(info_len,&buf[i]);
        i+=info_len;
        printf("</stream>\n");
      }
//      printf("</service>\n");
//      printf("</pmt>\n");
    } else {
      printf("Under-read bytes for PMT - wanted %d, got %d\n",seclen,n);
      close(fd_pmt);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_pmt\n");
  }
}
 close(fd_pmt);
}

void scan_pat() {
  int fd_pat;
  int n,seclen;
  int i;
  unsigned char buf[4096];
  struct dmxSctFilterParams sctFilterParams;
  struct pollfd ufd;

  pat_t pat;

  if((fd_pat = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_pat DEVICE: ");
      return;
  }

  sctFilterParams.pid=0x0;
  memset(&sctFilterParams.filter.filter,0,DMX_FILTER_SIZE);
  memset(&sctFilterParams.filter.mask,0,DMX_FILTER_SIZE);
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=0x0;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_pat,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("PAT - DMX_SET_FILTER:");
    close(fd_pat);
    return;
  }

  ufd.fd=fd_pat;
  ufd.events=POLLPRI;
  if (poll(&ufd,1,2000) < 0) {
     fprintf(stderr,"TIMEOUT reading from fd_pat\n");
     close(fd_pat);
     return;
  }
  if (read(fd_pat,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_pat,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
      //      printf("Read %d bytes - Found %d services\n",seclen,(seclen-11)/4);
    //    for (i=0;i<seclen+3;i++) { printf("%02x ",buf[i]); }
//      printf("<pat>\n");
      i=8;
      while (i < seclen-4) {
        pat.service_id=(buf[i]<<8)|buf[i+1];
        pat.pmt_pid=((buf[i+2]&0x1f)<<8)|buf[i+3];
        add_pat(pat);
	/*        if (service_id!=0) {
          scan_pmt(pmt_pid,service_id,(service_id==pnr));
        } else {
          printf("<service id=\"0\" pmt_pid=\"%d\">\n</service>\n",pmt_pid);
        }
	*/        i+=4;
      }
//      printf("</pat>\n");
    } else {
      printf("Under-read bytes for PAT - wanted %d, got %d\n",seclen,n);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_pat\n");
  }
  close(fd_pat);
}

void scan_sdt() {
  int fd_sdt;
  int n,seclen;
  int i,k;
  int max_k;
  unsigned char buf[4096];
  struct dmxSctFilterParams sctFilterParams;
  int ca,service_id,loop_length;
  struct pollfd ufd;

  if((fd_sdt = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_sdt DEVICE: ");
      return;
  }

  sctFilterParams.pid=0x11;
  memset(&sctFilterParams.filter.filter,0,DMX_FILTER_SIZE);
  memset(&sctFilterParams.filter.mask,0,DMX_FILTER_SIZE);
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=0x42;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_sdt,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("SDT - DMX_SET_FILTER:");
    close(fd_sdt);
    return;
  }

  max_k=1;
//  printf("<sdt>\n");

for (k=0;k<max_k;k++) {
 ufd.fd=fd_sdt;
 ufd.events=POLLPRI;
 if (poll(&ufd,1,2000) < 0 ) {
   fprintf(stderr,"TIMEOUT on read from fd_sdt\n");
   close(fd_sdt);
   return;
 }
  if (read(fd_sdt,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_sdt,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
//      printf("Read %d bytes\n",seclen);
    //    for (i=0;i<seclen+3;i++) { printf("%02x ",buf[i]); }
/*      for (i=0;i< seclen;i++) {
        printf("%02x ",buf[i]);
        if ((i % 16)==15) { 
          printf("  ");
          for (j=i-15;j<=i;j++) { 
             printf("%c",((buf[j]>31) && (buf[j]<=127)) ? buf[j] : '.'); 
          }
          printf("\n");
        }
      }
*/

      max_k=buf[7]+1; // last_sec_num - read this many (+1) sections

      i=11;
      while (i < (seclen-4)) {
       service_id=(buf[i]<<8)|buf[i+1];
       i+=2;
       i++;  // Skip a field
       ca=(buf[i]&0x10)>>4;
       loop_length=((buf[i]&0x0f)<<8)|buf[i+1];
       printf("<service id=\"%d\" ca=\"%d\">\n",service_id,ca);
       i+=2;
       parse_descriptors(loop_length,&buf[i]);
       i+=loop_length;
       scan_pmt(get_pmt_pid(service_id),service_id,(service_id==pnr));
       printf("</service>\n");
      }
    }  else {
      printf("Under-read bytes for SDT - wanted %d, got %d\n",seclen,n);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_sdt\n");
  }
}
//  printf("</sdt>\n");
  close(fd_sdt);

}

int FEReadBER(int fd, uint32_t *ber)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_BER, ber) < 0)){
                perror("FE READ_BER: ");
                return -1;
        }
        return 0;
}


int FEReadSignalStrength(int fd, int32_t *strength)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_SIGNAL_STRENGTH, strength) < 0)){
                perror("FE READ SIGNAL STRENGTH: ");
                return -1;
        }
        return 0;
}

int FEReadSNR(int fd, int32_t *snr)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_SNR, snr) < 0)){
                perror("FE READ_SNR: ");
                return -1;
        }
        return 0;
}

#if 0
int FEReadAFC(int fd, int32_t *snr)
{   
        int ans;

        if ( (ans = ioctl(fd,FE_READ_AFC, snr) < 0)){
                perror("FE READ_AFC: ");
                return -1;
        }
        return 0;
}
#endif


int FEReadUncorrectedBlocks(int fd, uint32_t *ucb)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_UNCORRECTED_BLOCKS, ucb) < 0)){
                perror("FE READ UNCORRECTED BLOCKS: ");
                return -1;
        }
        return 0;
}

int main(int argc, char **argv)
{
  int fd_frontend=0;
  int fd_sec=0;
  int fd_dvr=0;
  int do_info=0;
  int do_scan=0;
  int do_monitor=0;
	
  unsigned int freq=0;
  char pol=0;
  unsigned int srate=0;
  unsigned int diseqc = 0;
  int ttpid=0;
  int dpid=0;
  int count;
  transponder_t * t;

  int i;
  
  if (argc==1) {
    fprintf(stderr,"Usage: dvbtune [OPTIONS]\n\n");
    fprintf(stderr,"The following options are available:\n\n");
    fprintf(stderr,"-c [0-3]   Use DVB device [0-3]\n");
    fprintf(stderr,"-f freq    absolute Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"           or L-band Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"-p [H,V]   Polarity (DVB-S only)\n");
    fprintf(stderr,"-t [0|1]   0=22kHz off, 1=22kHz on\n");
    fprintf(stderr,"-I [0|1|2] 0=Spectrum Inversion off, 1=Spectrum Inversion on, 2=auto\n");
    fprintf(stderr,"-s N       Symbol rate (DVB-S only)\n");
    fprintf(stderr,"-D [0-4]   DiSEqC command (0=none)\n\n");

    fprintf(stderr,"-V vpid    Set video PID (full cards only)\n");
    fprintf(stderr,"-A apid    Set audio PID (full cards only)\n");
    fprintf(stderr,"-T ttpid   Set teletext PID (full cards only)\n");
    fprintf(stderr,"-pnr N     Tune to Program Number (aka service) N\n\n");

    fprintf(stderr,"-i         Dump SI information as XML\n");
    fprintf(stderr,"-x         Attempt to auto-find other transponders (experimental)\n");
    fprintf(stderr,"-m         Monitor the reception quality\n");
    fprintf(stderr,"-n dpid    Add network interface and receive MPE on PID dpid\n");
    fprintf(stderr,"\n");
    return(-1);
  } else {
    count=0;
    for (i=1;i<argc;i++) {
      if (strcmp(argv[i],"-f")==0) {
        i++;
        freq=atoi(argv[i]);
      } else if (strcmp(argv[i],"-i")==0) { // 
        do_info=1;
      } else if (strcmp(argv[i],"-m")==0) { // 
        do_monitor=1;
      } else if (strcmp(argv[i],"-n")==0) { // 
        i++;
        dpid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-c")==0) { // 
        i++;
        card=atoi(argv[i]);
        if ((card < 0) || (card > 3)) {
	  fprintf(stderr,"card must be between 0 and 3\n");
          exit(-1);
        }
      } else if (strcmp(argv[i],"-x")==0) { // 
        do_scan=1;
      } else if (strcmp(argv[i],"-V")==0) {
        i++;
        vpid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-pnr")==0) {
        i++;
        pnr=atoi(argv[i]);
        do_info=1;
      } else if (strcmp(argv[i],"-A")==0) {
        i++;
        apid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-T")==0) {
        i++;
        ttpid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-p")==0) {
        i++;
        if (argv[i][1]==0) {
	  if (tolower(argv[i][0])=='v') {
            pol='V';
          } else if (tolower(argv[i][0])=='h') {
            pol='H';
          }
        }
      } else if (strcmp(argv[i],"-s")==0) {
        i++;
        srate=atoi(argv[i])*1000UL;
      } else if (strcmp(argv[i],"-D")==0) {
        i++;
        diseqc=atoi(argv[i]);
        if (diseqc > 4) {
	  fprintf(stderr,"DiSEqC must be between 0 and 4\n");
          exit(-1);
        }
      } else if (strcmp(argv[i],"-t")==0) {
	i++;
	if (atoi(argv[i])==0)
	   tone = SEC_TONE_OFF;
        else
	   tone = SEC_TONE_ON;
      } else if (strcmp(argv[i],"-I")==0) {
        i++;
        if (atoi(argv[i])==0)
           specInv = INVERSION_OFF;
	else if (atoi(argv[i])==1)
           specInv = INVERSION_ON;
        else
           specInv = INVERSION_AUTO;
      }
    }
  }

  if (do_monitor) {
        int32_t strength, ber, snr, uncorr;
        FrontendStatus festatus;

        if((fd_frontend = open(frontenddev[card],O_RDONLY)) < 0){
                fprintf(stderr,"frontend: %d",i);
                perror("FRONTEND DEVICE: ");
                return -1;
        }

        // Check the signal strength and the BER
        while (1) {
                festatus = 0; strength = 0; ber = 0; snr = 0; uncorr = 0;
                FEReadBER(fd_frontend, &ber);
                FEReadSignalStrength(fd_frontend, &strength);
                FEReadSNR(fd_frontend, &snr);
                FEReadUncorrectedBlocks(fd_frontend, &uncorr);
                ioctl(fd_frontend,FE_READ_STATUS,&festatus);
                fprintf(stderr,"Signal=%d, Verror=%d, SNR=%ddB, BlockErrors=%d, (", strength, ber, snr, uncorr);
		if (festatus & FE_HAS_POWER) fprintf(stderr,"P|");
		if (festatus & FE_HAS_SIGNAL) fprintf(stderr,"S|");
		if (festatus & FE_SPECTRUM_INV) fprintf(stderr,"I|");
		if (festatus & FE_HAS_LOCK) fprintf(stderr,"L|");
		if (festatus & FE_HAS_CARRIER) fprintf(stderr,"C|");
		if (festatus & FE_HAS_VITERBI) fprintf(stderr,"V|");
		if (festatus & FE_HAS_SYNC) fprintf(stderr,"SY|");
		fprintf(stderr,")\n");
                sleep(1);
        }
  }


#if 0
  if (!((freq > 100000000) || ((freq > 0) && (pol!=0) && (srate!=0)))) {
    fprintf(stderr,"Invalid parameters\n");
    exit(-1);
  }
#endif

  if((fd_dvr = open(dvrdev[card],O_RDONLY|O_NONBLOCK)) < 0){
      fprintf(stderr,"FD %d: ",i);
      perror("fd_dvr DEMUX DEVICE: ");
      return -1;
  }

  if((fd_frontend = open(frontenddev[card],O_RDWR)) < 0){
      fprintf(stderr,"frontend: %d",i);
      perror("FRONTEND DEVICE: ");
      return -1;
  }

  /* Only open sec for DVB-S tuning */
  if (freq<100000000) {
    if((fd_sec = open(secdev[card],O_RDWR)) < 0) {
        fprintf(stderr,"FD %i: ",i);
        perror("SEC DEVICE (warning) ");
    }
  }

  if((fd_demuxrec = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxv = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxa = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxtt = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxd = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxsi = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if (freq > 0) {
    /* Stop the hardware filters */
    set_apid(0);
    set_vpid(0);
    set_ttpid(0);

    if (tune_it(fd_frontend,fd_sec,freq,srate,pol,tone,specInv,diseqc) < 0) {
      return -1;
    }
  }

  if (do_scan) {
    printf("<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>\n<satellite>\n");
    scan_nit(0x40); /* Get initial list of transponders */
    scan_nit(0x41); /* Get initial list of transponders */
    while ((t=get_unscanned(transponders))!=NULL) {
      free_pat_list();
      fprintf(stderr,"Scanning %d%c %d\n",t->freq,t->pol,t->srate);
      tune_it(fd_frontend,fd_sec,t->freq,t->srate,t->pol,tone,specInv,0);
      printf("<transponder id=\"%d\" onid=\"%d\" freq=\"%05d\" srate=\"%d\" pos=\"%04x\" we_flag=\"%d\" polarity=\"%c\" modulation=\"%d\">\n",t->id,t->onid,t->freq,t->srate,t->pos,t->we_flag,t->pol,t->mod);
      t->scanned=1;
      scan_pat();
      scan_sdt();
      printf("</transponder>\n");
      scan_nit(0x40); /* See if there are any new transponders */
      scan_nit(0x41); /* See if there are any new transponders */
    }
    printf("</satellite>\n");
  }
  if (do_info) {
    if (freq< 100000000) {
      printf("<transponder type=\"S\" freq=\"%d\" srate=\"%d\" polarity=\"%c\" >\n",freq,srate,pol);
    } else {
      printf("<transponder type=\"T\" freq=\"%d\">\n",freq);
    }
    scan_pat();
    scan_sdt();
//    scan_nit(0x40);
    printf("</transponder>\n");
  }

  if ((vpid!=0) || (apid!=0) || (ttpid!=0)) {
    set_vpid(vpid);
    set_apid(apid);
    set_ttpid(ttpid);
    fprintf(stderr,"A/V/TT Filters set\n");
  }

  if (dpid > 0) {
    char devnamen[80];
    int dev, fdn;
    struct dvb_net_if netif;

    dev = card;
    netif.pid = dpid;
    netif.if_num = 0;  // always choosen the next free number

    sprintf(devnamen,"/dev/ost/net%d",dev);
    //printf("Trying to open %s\n",devnamen);
    if((fdn = open(devnamen,O_RDWR|O_NONBLOCK)) < 0) {
      fprintf(stderr, "Failed to open DVB NET DEVICE");
      close(fd_frontend);
      close(fd_sec);
    } else {
      // Add the network interface
      ioctl( fdn,NET_ADD_IF,&netif);

      close (fdn);
      printf("Successfully opened network device, please configure the dvb interface\n");
    }
  }

  close(fd_frontend);
  if (fd_sec) close(fd_sec);
  return(0);
}


