/* 

dvbstream - RTP-ize a DVB transport stream.
(C) Dave Chapman <dave@dchapman.com> 2001, 2002.

The latest version can be found at http://www.linuxstb.org/dvbstream

Copyright notice:

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
*/

// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <resolv.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <values.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

// DVB includes:
#ifdef NEWSTRUCT
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#else
#include <ost/dmx.h>
#include <ost/sec.h>
#include <ost/frontend.h>
#endif

#include "rtp.h"
#include "mpegtools/transform.h"
#include "mpegtools/remux.h"

#include "tune.h"

// The default telnet port.
#define DEFAULT_PORT 12345

#define USAGE "\nUSAGE: dvbstream tpid1 tpid2 tpid3 .. tpid8\n\n"
#define PACKET_SIZE 188

/* Thanks to Giancarlo Baracchino for this fix */
#define MTU 1500
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8
#define RTP_HEADER_SIZE 12

#define MAX_RTP_SIZE (MTU-IP_HEADER_SIZE-UDP_HEADER_SIZE-RTP_HEADER_SIZE)

#define writes(f,x) write((f),(x),strlen(x))

/* Signal handling code shamelessly copied from VDR by Klaus Schmidinger 
   - see http://www.cadsoft.de/people/kls/vdr/index.htm */

#ifdef NEWSTRUCT
char* frontenddev[4]={"/dev/dvb/adapter0/frontend0","/dev/dvb/adapter1/frontend0","/dev/dvb/adapter2/frontend0","/dev/dvb/adapter3/frontend0"};
char* dvrdev[4]={"/dev/dvb/adapter0/dvr0","/dev/dvb/adapter1/dvr0","/dev/dvb/adapter2/dvr0","/dev/dvb/adapter3/dvr0"};
char* demuxdev[4]={"/dev/dvb/adapter0/demux0","/dev/dvb/adapter1/demux0","/dev/dvb/adapter2/demux0","/dev/dvb/adapter3/demux0"};
#else
char* frontenddev[4]={"/dev/ost/frontend0","/dev/ost/frontend1","/dev/ost/frontend2","/dev/ost/frontend3"};
char* dvrdev[4]={"/dev/ost/dvr0","/dev/ost/dvr1","/dev/ost/dvr2","/dev/ost/dvr3"};
char* secdev[4]={"/dev/ost/sec0","/dev/ost/sec1","/dev/ost/sec2","/dev/ost/sec3"};
char* demuxdev[4]={"/dev/ost/demux0","/dev/ost/demux1","/dev/ost/demux2","/dev/ost/demux3"};
#endif

int card=0;

int Interrupted=0;
fe_spectral_inversion_t specInv=INVERSION_AUTO;
int tone=-1;
fe_modulation_t modulation=CONSTELLATION_DEFAULT;
fe_transmit_mode_t TransmissionMode=TRANSMISSION_MODE_DEFAULT;
fe_bandwidth_t bandWidth=BANDWIDTH_DEFAULT;
fe_guard_interval_t guardInterval=GUARD_INTERVAL_DEFAULT;
fe_code_rate_t HP_CodeRate=HP_CODERATE_DEFAULT;
unsigned int diseqc=0;
char pol=0;

int open_fe(int* fd_frontend,int* fd_sec) {

    if((*fd_frontend = open(frontenddev[card],O_RDWR)) < 0){
        perror("FRONTEND DEVICE: ");
        return -1;
    }
#ifdef NEWSTRUCT
    fd_sec=0;
#else
    if (fd_sec!=0) {
      if((*fd_sec = open(secdev[card],O_RDWR)) < 0){
          perror("SEC DEVICE: ");
          return -1;
      }
    }
#endif
    return 1;
}

static void SignalHandler(int signum) {
  if (signum != SIGPIPE) {
    Interrupted=signum;
  }
  signal(signum,SignalHandler);
}

long getmsec() {
  struct timeval tv;
  gettimeofday(&tv,(struct timezone*) NULL);
  return(tv.tv_sec%1000000)*1000 + tv.tv_usec/1000;
}

// There seems to be a limit of 8 simultaneous filters in the driver
#define MAX_CHANNELS 8

void set_ts_filt(int fd,uint16_t pid, dmx_pes_type_t pestype)
{
  struct dmx_pes_filter_params pesFilterParams;

  pesFilterParams.pid     = pid;
  pesFilterParams.input   = DMX_IN_FRONTEND;
  pesFilterParams.output  = DMX_OUT_TS_TAP;
#ifdef NEWSTRUCT
  pesFilterParams.pes_type = pestype;
#else
  pesFilterParams.pesType = pestype;
#endif
  pesFilterParams.flags   = DMX_IMMEDIATE_START;

  if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
    fprintf(stderr,"FILTER %i: ",pid);
    perror("DMX SET PES FILTER");
  }
}

void make_nonblock(int f) {
  int oldflags;

  if ((oldflags=fcntl(f,F_GETFL,0)) < 0) {
    perror("F_GETFL");
  }
  oldflags|=O_NONBLOCK;
  if (fcntl(f,F_SETFL,oldflags) < 0) {
    perror("F_SETFL");
  }
}

typedef enum {STREAM_ON,STREAM_OFF} state_t;


  int socketIn, ns;
  int pids[MAX_CHANNELS];
  int pestypes[MAX_CHANNELS];
  unsigned char hi_mappids[8192];
  unsigned char lo_mappids[8192];
  int fd_sec;
  int fd_frontend;
  int pid,pid2;
  int connectionOpen;
  int fromlen;
  char hostname[64];
  char in_ch;
  struct hostent *hp;
  struct sockaddr_in name, fsin;
  int ReUseAddr=1;
  int oldflags;
  int npids = 0;
  int fd[MAX_CHANNELS];
  int to_stdout = 0; /* to stdout instead of rtp stream */
  /* rtp */
  struct rtpheader hdr;
  struct sockaddr_in sOut;
  int socketOut;

  ipack pa, pv;

#define IPACKS 2048
#define TS_SIZE 188
#define IN_SIZE TS_SIZE

int process_telnet() {
  char cmd[1024];
  int cmd_i=0;
  int i;
  char* ch;
  dmx_pes_type_t pestype;
  unsigned long freq=0;
  unsigned long srate=0;

    /* Open a new telnet session if a client is trying to connect */
    if (ns==-1) {
      if ((ns = accept(socketIn, (struct sockaddr *)&fsin, &fromlen)) > 0) {
        make_nonblock(ns);
        cmd_i=0;      
        cmd[0]=0;
        printf("Opened connection\n");
        writes(ns,"220-DVBSTREAM - ");
        writes(ns,hostname);
        writes(ns,"\r\nDONE\r\n");
        connectionOpen=1;
      }
    }

    /* If a telnet session is open, receive and process any input */
    if (connectionOpen) {
      /* Read in at most a line of text - any ctrl character ends the line */
      while (read(ns,&in_ch,1)>0) {
          if (in_ch < 32) break;
          /* Prevent buffer overflows */
          if (cmd_i < 1024-1) {
            cmd[cmd_i++]=in_ch;
            cmd[cmd_i]=0;
          }
      }
      if (in_ch > 0) {
        if (cmd_i > 0) {
          printf("CMD: \"%s\"\n",cmd);
          if (strcasecmp(cmd,"QUIT")==0) {
            writes(ns,"DONE\r\n");
            close(ns);
            ns=-1;
            connectionOpen=0; 
            printf("Closed connection\n");
          } else if (strcasecmp(cmd,"STOP")==0) {
            writes(ns,"STOP\n");
            for (i=0;i<npids;i++) {
              if (ioctl(fd[i], DMX_STOP) < 0)  {
                 perror("DMX_STOP");
              }
            }
            for (i=0;i<8192;i++) {
              hi_mappids[i]=(i >> 8);
              lo_mappids[i]=(i&0xff);
            }
            writes(ns,"DONE\r\n");
          } else if (strncasecmp(cmd,"ADD",3)==0) {
            i=4;
            if ((cmd[3]=='V') || (cmd[3]=='v')) pestype=DMX_PES_VIDEO;
            else if ((cmd[3]=='A') || (cmd[3]=='a')) pestype=DMX_PES_AUDIO;
            else if ((cmd[3]=='T') || (cmd[3]=='t')) pestype=DMX_PES_TELETEXT;
            else { pestype=DMX_PES_OTHER; i=3; }
            while (cmd[i]==' ') i++;
            if ((ch=(char*)strstr(&cmd[i],":"))!=NULL) {
              pid2=atoi(&ch[1]);
              ch[0]=0;
            } else {
              pid2=-1;
            }
            pid=atoi(&cmd[i]);
            if (pid) {
              if (npids == MAX_CHANNELS) {
                fprintf(stderr,"\nsorry, you can only set up to 8 filters.\n\n");
                return(-1);
              } else {
                pestypes[npids]=pestype;
                pestype=DMX_PES_OTHER;
                pids[npids]=pid;
                if (pid2!=-1) {
                  hi_mappids[pid]=pid2>>8;
                  lo_mappids[pid]=pid2&0xff;
                  fprintf(stderr,"Mapping %d to %d\n",pid,pid2);
                }
                
                if((fd[npids] = open(demuxdev[card],O_RDWR)) < 0){
                  fprintf(stderr,"FD %i: ",i);
                  perror("DEMUX DEVICE: ");
                } else {
                  set_ts_filt(fd[npids],pids[npids],pestypes[npids]);
                  npids++;
                }
              }
            }
            writes(ns,"DONE\r\n");
          } else if (strcasecmp(cmd,"START")==0) {
            writes(ns,"START\n");
            for (i=0;i<npids;i++) {
              set_ts_filt(fd[i],pids[i],pestypes[i]);
            }
            writes(ns,"DONE\r\n");
          } else if (strncasecmp(cmd,"TUNE",4)==0) {
            for (i=0;i<8192;i++) {
              hi_mappids[i]=(i >> 8);
              lo_mappids[i]=(i&0xff);
            }
            for (i=0;i<npids;i++) {
              if (ioctl(fd[i], DMX_STOP) < 0)  {
                 perror("DMX_STOP"); 
                 close(fd[i]);
              }
            }
            npids=0;
            i=4;
            while (cmd[i]==' ') i++;
            freq=atoi(&cmd[i])*1000UL;
            while ((cmd[i]!=' ') && (cmd[i]!=0)) i++;
            if (cmd[i]!=0) {
              while (cmd[i]==' ') i++;
              pol=cmd[i];
              while ((cmd[i]!=' ') && (cmd[i]!=0)) i++;
              if (cmd[i]!=0) {
                while (cmd[i]==' ') i++;
                srate=atoi(&cmd[i])*1000UL;
                if (open_fe(&fd_frontend,&fd_sec)) {
                  fprintf(stderr,"Tuning to %ld,%ld,%c\n",freq,srate,pol);
                  tune_it(fd_frontend,fd_sec,freq,srate,pol,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
                  close(fd_frontend);
                  if (fd_sec) close(fd_sec);
                }
              }
            }
          }
          cmd_i=0;
          cmd[0]=0;
          writes(ns,"DONE\r\n");
        }
      }
    }
    return(0);
}


/* The output routine for sending a PS */
void my_write_out(uint8_t *buf, int count,void  *p)
{
  /* to fix: change this buffer size and check for overflow */
  static uint8_t out_buffer[1000000];
  static int out_buffer_n=0;
  int i;

  if (to_stdout) {
    /* This one is easy. */

    write(STDOUT_FILENO, buf, count);
  } else { /* We are streaming it. */
    /* Copy data to write to the end of out_buffer */

    memcpy(&out_buffer[out_buffer_n],buf,count);
    out_buffer_n+=count;

    /* Send as many full packets as possible */

    i=0;
    while ((i + MAX_RTP_SIZE) < out_buffer_n) {
       hdr.timestamp = getmsec()*90;
       sendrtp2(socketOut,&sOut,&hdr,&out_buffer[i],MAX_RTP_SIZE);
       i+=MAX_RTP_SIZE;
    }

    /* Move whatever data is left to the start of the buffer */

    memmove(&out_buffer[0],&out_buffer[i],out_buffer_n-i);
    out_buffer_n-=i;
  }
}

void my_ts_to_ps( uint8_t* buf, uint16_t pida, uint16_t pidv)
{
  uint16_t pid;
  ipack *p;
  uint8_t off = 0;

  pid = get_pid(buf+1);
  if (!(buf[3]&0x10)) // no payload?
    return;
  if (pid == pidv){
    p = &pv;
  } else {
    if (pid == pida){
      p = &pa;
    } else return;
  }

  if ( buf[1]&0x40) {
    if (p->plength == MMAX_PLENGTH-6){
      p->plength = p->found-6;
      p->found = 0;
      send_ipack(p);
      reset_ipack(p);
    }
  }

  if ( buf[3] & 0x20) {  // adaptation field?
    off = buf[4] + 1;
  }
        
  instant_repack(buf+4+off, TS_SIZE-4-off, p);
}


int main(int argc, char **argv)
{
  //  state_t state=STREAM_OFF;
  unsigned short int port=DEFAULT_PORT;
  int fd_dvr;
  int i;
  unsigned char buf[MTU];
  struct pollfd pfds[2];  // DVR device and Telnet connection
  unsigned int secs = 0;
  unsigned long freq=0;
  unsigned long srate=0;
  int count;
  char* ch;
  dmx_pes_type_t pestype;
  int bytes_read;
  unsigned char* free_bytes;
  int output_type=RTP_TS;

  /* Output: {uni,multi,broad}cast socket */
  char ipOut[20];
  int portOut;
  int ttl;

  fprintf(stderr,"dvbstream v0.4pre3 - (C) Dave Chapman 2001\n");
  fprintf(stderr,"Released under the GPL.\n");
  fprintf(stderr,"Latest version available from http://www.linuxstb.org/\n");

  /* Initialise PID map */
  for (i=0;i<8192;i++) {
    hi_mappids[i]=(i >> 8);
    lo_mappids[i]=(i&0xff);
  }

  /* Set default IP and port */
  strcpy(ipOut,"224.0.1.2");
  portOut = 5004;

  if (argc==1) {
    fprintf(stderr,"Usage: dvbtune [OPTIONS] pid1 pid2 ... pid8\n\n");
    fprintf(stderr,"-i          IP multicast address\n");
    fprintf(stderr,"-r          IP multicast port\n");
    fprintf(stderr,"-o          Stream to stdout instead of network\n");
    fprintf(stderr,"-n secs     Stop after secs seconds\n");
    fprintf(stderr,"-ps         Convert stream to Program Stream format (needs exactly 2 pids)\n");
    fprintf(stderr,"-v vpid     Decode video PID (full cards only)\n");
    fprintf(stderr,"-a apid     Decode audio PID (full cards only)\n");
    fprintf(stderr,"-t ttpid    Decode teletext PID (full cards only)\n");
    fprintf(stderr,"\nStandard tuning options:\n\n");
    fprintf(stderr,"-f freq     absolute Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"            or L-band Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"-p [H,V]    Polarity (DVB-S only)\n");
    fprintf(stderr,"-s N        Symbol rate (DVB-S or DVB-C)\n");

    fprintf(stderr,"\nAdvanced tuning options:\n\n");
    fprintf(stderr,"-c [0-3]    Use DVB card #[0-3]\n");
    fprintf(stderr,"-qam X      DVB-T modulation - 16%s, 32%s, 64%s, 128%s or 256%s\n",(CONSTELLATION_DEFAULT==QAM_16 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_32 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_64 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_128 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_256 ? " (default)" : ""));
    fprintf(stderr,"-gi N       DVB-T guard interval 1_N (N=32%s, 16%s, 8%s or 4%s)\n",(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_32 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_16 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_8 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_4 ? " (default)" : ""));
    fprintf(stderr,"-cr N       DVB-T code rate. N=AUTO%s, 1_2%s, 2_3%s, 3_4%s, 5_6%s, 7_8%s\n",(HP_CODERATE_DEFAULT==FEC_AUTO ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_1_2 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_2_3 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_3_4 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_5_6 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_7_8 ? " (default)" : ""));
    fprintf(stderr,"-bw N       DVB-T bandwidth (Mhz) - N=6%s, 7%s or 8%s\n",(BANDWIDTH_DEFAULT==BANDWIDTH_6_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_7_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_8_MHZ ? " (default)" : ""));
    fprintf(stderr,"-tm N       DVB-T transmission mode - N=2%s or 8%s\n",(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_2K ? " (default)" : ""),(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_8K ? " (default)" : ""));

    fprintf(stderr,"\n");
    fprintf(stderr,"NOTE: Use pid1=8192 to broadcast whole TS stream from a budget card\n");
    return(-1);
  } else {
    npids=0;
    pestype=DMX_PES_OTHER;  // Default PES type
    for (i=1;i<argc;i++) {
      if (strcmp(argv[i],"-ps")==0) {
        output_type=RTP_PS;
      } else if (strcmp(argv[i],"-i")==0) {
        i++;
        strcpy(ipOut,argv[i]);
      } else if (strcmp(argv[i],"-r")==0) {
        i++;
        portOut=atoi(argv[i]);
      } else if (strcmp(argv[i],"-f")==0) {
        i++;
        freq=atoi(argv[i])*1000UL;
      } else if (strcmp(argv[i],"-p")==0) {
        i++;
        if (argv[i][1]==0) {
    if (tolower(argv[i][0])=='v') {
            pol='V';
          } else if (tolower(argv[i][0])=='h') {
            pol='H';
          }
        }
      } 
      else if (strcmp(argv[i],"-s")==0) {
        i++;
        srate=atoi(argv[i])*1000UL;
      } 
      else if (strcmp(argv[i],"-D")==0) 
      {
        i++;
        diseqc=atoi(argv[i]);
	if(diseqc < 0 || diseqc > 4)
		diseqc = 0;	
      } 
      else if (strcmp(argv[i],"-o")==0) {
        to_stdout = 1;
      } else if (strcmp(argv[i],"-n")==0) {
        i++;
        secs=atoi(argv[i]);
      } else if (strcmp(argv[i],"-c")==0) {
        i++;
        card=atoi(argv[i]);
        if ((card < 0) || (card > 3)) {
          fprintf(stderr,"ERROR: card parameter must be between 0 and 4\n");
        }
      } else if (strcmp(argv[i],"-v")==0) {
        pestype=DMX_PES_VIDEO;
      } else if (strcmp(argv[i],"-a")==0) {
        pestype=DMX_PES_AUDIO;
      } else if (strcmp(argv[i],"-t")==0) {
        pestype=DMX_PES_TELETEXT;
      } else if (strcmp(argv[i],"-qam")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 16:  modulation=QAM_16; break;
          case 32:  modulation=QAM_32; break;
          case 64:  modulation=QAM_64; break;
          case 128: modulation=QAM_128; break;
          case 256: modulation=QAM_256; break;
          default:
            fprintf(stderr,"Invalid QAM rate: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-gi")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 32:  guardInterval=GUARD_INTERVAL_1_32; break;
          case 16:  guardInterval=GUARD_INTERVAL_1_16; break;
          case 8:   guardInterval=GUARD_INTERVAL_1_8; break;
          case 4:   guardInterval=GUARD_INTERVAL_1_4; break;
          default:
            fprintf(stderr,"Invalid Guard Interval: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-tm")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 8:   TransmissionMode=TRANSMISSION_MODE_8K; break;
          case 2:   TransmissionMode=TRANSMISSION_MODE_2K; break;
          default:
            fprintf(stderr,"Invalid Transmission Mode: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-bw")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 8:   bandWidth=BANDWIDTH_8_MHZ; break;
          case 7:   bandWidth=BANDWIDTH_7_MHZ; break;
          case 6:   bandWidth=BANDWIDTH_6_MHZ; break;
          default:
            fprintf(stderr,"Invalid DVB-T bandwidth: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-cr")==0) {
        i++;
        if (!strcmp(argv[i],"AUTO")) {
          HP_CodeRate=FEC_AUTO;
        } else if (!strcmp(argv[i],"1_2")) {
          HP_CodeRate=FEC_1_2;
        } else if (!strcmp(argv[i],"2_3")) {
          HP_CodeRate=FEC_2_3;
        } else if (!strcmp(argv[i],"3_4")) {
          HP_CodeRate=FEC_3_4;
        } else if (!strcmp(argv[i],"5_6")) {
          HP_CodeRate=FEC_5_6;
        } else if (!strcmp(argv[i],"7_8")) {
          HP_CodeRate=FEC_7_8;
        } else {
          fprintf(stderr,"Invalid Code Rate: %s\n",argv[i]);
          exit(0);
        }
      } else {
        if ((ch=(char*)strstr(argv[i],":"))!=NULL) {
          pid2=atoi(&ch[1]);
          ch[0]=0;
        } else {
          pid2=-1;
        }
        pid=atoi(argv[i]);
        if (pid) {
          if (npids == MAX_CHANNELS) {
            fprintf(stderr,"\nSorry, you can only set up to 8 filters.\n\n");
            return(-1);
          } else {
            pestypes[npids]=pestype;
            pestype=DMX_PES_OTHER;
            pids[npids++]=pid;
            if (pid2!=-1) {
              hi_mappids[pid]=pid2>>8;
              lo_mappids[pid]=pid2&0xff;
              fprintf(stderr,"Mapping %d to %d\n",pid,pid2);
            }
         }
        }
      }
    }
  }

  if ((output_type==RTP_PS) && (npids!=2)) {
     fprintf(stderr,"ERROR: PS requires exactly two PIDS - video and audio.\n");
     exit;
 }
  if (signal(SIGHUP, SignalHandler) == SIG_IGN) signal(SIGHUP, SIG_IGN);
  if (signal(SIGINT, SignalHandler) == SIG_IGN) signal(SIGINT, SIG_IGN);
  if (signal(SIGTERM, SignalHandler) == SIG_IGN) signal(SIGTERM, SIG_IGN);
  if (signal(SIGALRM, SignalHandler) == SIG_IGN) signal(SIGALRM, SIG_IGN);

  if ( (freq>100000000)) {
    if (open_fe(&fd_frontend,0)) {
      i=tune_it(fd_frontend,0,freq,0,0,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
      close(fd_frontend);
    }
  } else if ((freq!=0) && (pol!=0) && (srate!=0)) {
    if (open_fe(&fd_frontend,&fd_sec)) {
      i=tune_it(fd_frontend,fd_sec,freq,srate,pol,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
      close(fd_frontend);
      if (fd_sec) close(fd_sec);
    }
  }

  if (i<0) { exit(i); }

  for (i=0;i<npids;i++) {  
    if((fd[i] = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
    }
  }

  if((fd_dvr = open(dvrdev[card],O_RDONLY|O_NONBLOCK)) < 0){
    perror("DVR DEVICE: ");
    return -1;
  }

  /* Now we set the filters */
  for (i=0;i<npids;i++) set_ts_filt(fd[i],pids[i],pestypes[i]);

  if (to_stdout) {
    fprintf(stderr,"Output to stdout\n");
  }
  else {
    ttl = 2;
    fprintf(stderr,"Using %s:%d:%d\n",ipOut,portOut,ttl);

    /* Init RTP */
    socketOut = makesocket(ipOut,portOut,ttl,&sOut);
    #warning WHAT SHOULD THE PAYLOAD TYPE BE FOR "MPEG-2 PS" ?
    initrtp(&hdr,(output_type==RTP_TS ? 33 : 34));
    fprintf(stderr,"version=%X\n",hdr.b.v);
  }
  fprintf(stderr,"Streaming %d stream%s\n",npids,(npids==1 ? "" : "s"));

  if (output_type==RTP_PS) {
    init_ipack(&pa, IPACKS,my_write_out, 1);
    init_ipack(&pv, IPACKS,my_write_out, 1);
  }

  /* Read packets */
  free_bytes = buf;

  /* Setup socket to accept input from a client */
  gethostname(hostname, sizeof(hostname));
  if ((hp = gethostbyname(hostname)) == NULL) {
    fprintf(stderr, "%s: host unknown.\n", hostname);
    exit(1);
  }
  if ((socketIn = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("server: socket");
    exit(1);
  }
  setsockopt(socketIn,SOL_SOCKET,SO_REUSEADDR,&ReUseAddr,sizeof(ReUseAddr));

  name.sin_family = AF_INET;
  name.sin_port = htons(port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(socketIn,(struct sockaddr* )&name,sizeof(name)) < 0) {
    perror("server: bind");
    exit(1);
  }

  make_nonblock(socketIn);

  if (listen(socketIn, 1) < 0) {
    perror("server: listen");
    exit(1);
  }

  connectionOpen=0;
  ns=-1;
  pfds[0].fd=fd_dvr;
  pfds[0].events=POLLIN|POLLPRI;
  pfds[1].events=POLLIN|POLLPRI;

  /* Set up timer */
  if (secs > 0) alarm(secs);
  while ( !Interrupted) {
    /* Poll the open file descriptors */
    if (ns==-1) {
        poll(pfds,1,500);
    } else {
        pfds[1].fd=ns;  // This can change
        poll(pfds,2,500);
    }

    process_telnet();  // See if there is an incoming telnet connection

    if (output_type==RTP_TS) {
      /* Attempt to read 188 bytes from /dev/ost/dvr */
      if ((bytes_read = read(fd_dvr,free_bytes,PACKET_SIZE)) > 0) {
        if (bytes_read!=PACKET_SIZE) {
          fprintf(stderr,"No bytes left to read - aborting\n");
          break;
        }

        pid=((free_bytes[1]&0x1f) << 8) | (free_bytes[2]);
        free_bytes[1]=(free_bytes[1]&0xe0)|hi_mappids[pid];
        free_bytes[2]=lo_mappids[pid];
        free_bytes+=bytes_read;

        // If there isn't enough room for 1 more packet, then send it.
        if ((free_bytes+PACKET_SIZE-buf)>MAX_RTP_SIZE) {
          hdr.timestamp = getmsec()*90;
          if (to_stdout) {
            write(1, buf, free_bytes-buf);
          } else {
            sendrtp2(socketOut,&sOut,&hdr,buf,free_bytes-buf);
          }
          free_bytes = buf;
        }
        count++;
      }
    } else {
       if (read(fd_dvr,buf,TS_SIZE) > 0) {
         my_ts_to_ps((uint8_t*)buf, pids[0], pids[1]);
       }
    }
  }

  if (Interrupted) {
    fprintf(stderr,"Caught signal %d - closing cleanly.\n",Interrupted);
  }

  if (ns!=-1) close(ns);
  close(socketIn);

  if (!to_stdout) close(socketOut);
  for (i=0;i<npids;i++) close(fd[i]);
  close(fd_dvr);
  return(0);
}
