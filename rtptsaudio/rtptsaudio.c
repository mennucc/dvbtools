/*
 *  rtpts2audio - a rtp-multicast mpeg-2 transport stream client
 *
 * Copyright (C) 2001 Dave Chapman
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 */


#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <resolv.h>
#include <errno.h>
#include <signal.h>

#include <linux/soundcard.h>

#include "libmad/mad.h"

#include "mpegtools/remux.h"
#include "mpegtools/transform.h"
#include "mpegtools/ctools.h"
#include "mpegtools/ringbuffy.h"

#include "rtp.h"

#define TS_SIZE 188
#define IPACKS 2048

#define ProgName "rtptsaudio"

/* Defines for LPCM Mpeg packets */

#define MAX_FRAMESIZE 2048
#define HDR_SIZE      9
#define LPCM_SIZE     7
#define LEN_CORR      3

/* The "dither" code to convert the 24-bit samples produced by libmad was
   taken from the coolplayer project - coolplayer.sourceforge.net */

struct dither {
	mad_fixed_t error[3];
	mad_fixed_t random;
};
# define SAMPLE_DEPTH	16
# define scale(x, y)	dither((x), (y))

/*
 * NAME:		prng()
 * DESCRIPTION:	32-bit pseudo-random number generator
 */
static __inline
unsigned long prng(unsigned long state)
{
  return (state * 0x0019660dL + 0x3c6ef35fL) & 0xffffffffL;
}

/*
 * NAME:        dither()
 * DESCRIPTION:	dither and scale sample
 */
static __inline
signed int dither(mad_fixed_t sample, struct dither *dither)
{
  unsigned int scalebits;
  mad_fixed_t output, mask, random;

  enum {
    MIN = -MAD_F_ONE,
    MAX =  MAD_F_ONE - 1
  };

  /* noise shape */
  sample += dither->error[0] - dither->error[1] + dither->error[2];

  dither->error[2] = dither->error[1];
  dither->error[1] = dither->error[0] / 2;

  /* bias */
  output = sample + (1L << (MAD_F_FRACBITS + 1 - SAMPLE_DEPTH - 1));

  scalebits = MAD_F_FRACBITS + 1 - SAMPLE_DEPTH;
  mask = (1L << scalebits) - 1;

  /* dither */
  random  = prng(dither->random);
  output += (random & mask) - (dither->random & mask);

  dither->random = random;

  /* clip */
  if (output > MAX) {
    output = MAX;

    if (sample > MAX)
      sample = MAX;
  }
  else if (output < MIN) {
    output = MIN;

    if (sample < MIN)
      sample = MIN;
  }

  /* quantize */
  output &= ~mask;

  /* error feedback */
  dither->error[0] = sample - output;

  /* scale */
  return output >> scalebits;
}

struct LPCMFrame {
  unsigned char PES[HDR_SIZE];
  unsigned char LPCM[LPCM_SIZE];
  unsigned char Data[MAX_FRAMESIZE-HDR_SIZE-LPCM_SIZE];
  };

/* To init LPCM Frame */
/*
  memset(&lpcmFrame,0,sizeof(lpcmFrame));
  lpcmFrame.PES[2]=0x01;
  lpcmFrame.PES[3]=0xbd;
  lpcmFrame.PES[6]=0x80;
  lpcmFrame.LPCM[0]=0xa0; // substream ID
  lpcmFrame.LPCM[1]=0xff;
  lpcmFrame.LPCM[5]=0x01;
  lpcmFrame.LPCM[6]=0x80;
*/

int secs;
int Interrupted;

char* user_outfile=NULL;

int sound=0;
struct mad_stream  Stream;
struct mad_frame  Frame;
struct mad_synth  Synth;
mad_timer_t      Timer;

#define OUTPUT_BUFFER_SIZE  8192 /* Must be an integer multiple of 4. */
  unsigned char OutputBuffer[OUTPUT_BUFFER_SIZE],
               *OutputPtr=OutputBuffer;
  const unsigned char  *OutputBufferEnd=OutputBuffer+OUTPUT_BUFFER_SIZE;
  int Status=0;

enum { AUDIO_OSS, AUDIO_MPA, AUDIO_PCM };

int output_type;

/* The status of the MPEG audio parser */
enum { MPA_UNKNOWN,  /* Unknown - start of decoding, or error */
       MPA_START,    /* We are at the start of a frame */
       MPA_MIDFRAME  /* We are in the middle of a frame */
     };
int mpa_status=MPA_UNKNOWN;

int mpa_bytesleft;   /* The number of bytes left to write in the current frame */

typedef struct TS2ES_BUF_T {
  uint8_t* buf;
  int count;
  ipack p;
  uint16_t pidv;
} ts2es_buf_t;

ts2es_buf_t ts2es_buf;

/* A buffer to store the output stream */
uint8_t mpa_buf[16384]; 
int mpa_buflen=0;

int mpa_bitrates[16]={0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0};
int mpa_freqs[4]={44100,48000,32000,0};
char* mpa_modes[4]={"stereo","joint-stereo","dual channel","mono"};

int sound_freq;

void init_oss() {
  int channels=1;
  int format=AFMT_U16_LE;
  int setting=0x000C000D;  // 12 fragments size 8kb ? WHAT IS THIS?

  sound=open((user_outfile==NULL) ? "/dev/dsp" : user_outfile, O_WRONLY);

  if (sound < 0) {
    fprintf(stderr,"Can not open %s - Aborting\n",(user_outfile==NULL) ? "/dev/dsp" : user_outfile);
    exit(-1);
  }

  if (ioctl(sound,SNDCTL_DSP_SETFRAGMENT,&setting)==-1) {
    perror("SNDCTL_DSP_SETFRAGMENT");
  }

  if (ioctl(sound,SNDCTL_DSP_STEREO,&channels)==-1) {
    perror("SNDCTL_DSP_STEREO");
  }
  if (channels==0) { fprintf(stderr,"Warning, only mono supported\n"); }

  if (ioctl(sound,SNDCTL_DSP_SETFMT,&format)==-1) {
    perror("SNDCTL_DSP_SETFMT");
  }

  fprintf(stderr,"SETTING %s to %dHz\n",(user_outfile==NULL) ? "/dev/dsp" : user_outfile,sound_freq);
  if (ioctl(sound,SNDCTL_DSP_SPEED,&sound_freq)==-1) {
    perror("SNDCTL_DSP_SPEED");
  }
}

void init_file() {
  if (user_outfile==NULL) {  
    sound=STDOUT_FILENO;
  } else {
    sound=open(user_outfile, O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
    if (sound < 0) {
      fprintf(stderr,"Can not open file %s.  Aborting\n",user_outfile);
      exit(-1);
    }
  }
}

void init_sound() {
  switch (output_type) {
    case AUDIO_OSS: init_oss();
                    break;
    case AUDIO_MPA: 
    case AUDIO_PCM: init_file();
                    break;
  }
}

int calc_frame_length(uint8_t* buf) {
  int id,layer,bitrate,framesize,freq,channel_mode,padding;

  id=(buf[1]&0x08);
  layer=(buf[1]&0x06);
    if ((id!=0x08) || (layer!=0x04)) {
      //    fprintf(stderr,"File isn't MPEG-1 Layer 2 - id: %02x layer: %02x - aborting\n",id,layer);
      return -1;
    }

  bitrate=(buf[2]&0xf0)>>4;
  freq=(buf[2]&12)>>2;
  channel_mode=((buf[3]&0xc0)>>6);
  padding=(buf[2]&0x02)>>1;
  sound_freq=mpa_freqs[freq];
  fprintf(stderr,"FOUND HEADER: MPEG 1.0 layer II, %d kbit/s, %d Hz %s\n",mpa_bitrates[bitrate],mpa_freqs[freq],mpa_modes[channel_mode]);
  framesize=((144000*mpa_bitrates[bitrate])/mpa_freqs[freq])+padding;
  return(framesize);
}

int find_frame_start() {
  int i=0;
  int frame_length=0;
  while ((i<(mpa_buflen-2)) && ((mpa_buf[i]!=0xff) || ((mpa_buf[i+1]&0xf0)!=0xf0))) {
    i++;
  }

  if ((mpa_buf[i]==0xff) && ((mpa_buf[i+1]&0xf0)==0xf0)) {
    frame_length=calc_frame_length(&mpa_buf[i]);
    if ((mpa_buf[i+frame_length]==0xff) && ((mpa_buf[i+frame_length+1]&0xf0)==0xf0)) {
      return(i);
    } else {
//      fprintf(stderr,"ERROR: CAN NOT SYNC TO STREAM - IS IT MPEG?\n");
      return(-1);
    }
  } else {
   return(-1);
  }
}

unsigned char InputBuffer[(50*8192)];

void output_mpa_frames() {
  int i;
  int Remaining;

 if (mpa_buflen > 0) {
  if (mpa_status==MPA_UNKNOWN) {
    i=find_frame_start();
    if (i < 0) {
//      fprintf(stderr,"SKIPPING %d bytes at start of stream\n",mpa_buflen-1);
      mpa_buf[0]=mpa_buf[mpa_buflen-1];
      mpa_buflen=1;
    } else {
//      fprintf(stderr,"SKIPPING %d bytes at start of stream\n",i);
      memmove(mpa_buf,&mpa_buf[i],mpa_buflen-i);
      mpa_buflen-=i;
      mpa_status=MPA_START;
    }
  }

  if (mpa_status!=MPA_UNKNOWN) {
    if (sound==0) {
      init_sound();
    }
    
    if (output_type==AUDIO_MPA) {    
      write(sound,mpa_buf, mpa_buflen);
      mpa_buflen=0;
    } else {
      if (Stream.next_frame!=NULL) {
        Remaining=Stream.bufend-Stream.next_frame;
//        fprintf(stderr,"Remaining: %d\n",Remaining);
        memcpy(InputBuffer,Stream.next_frame,Remaining);
      } else { 
        Remaining=0;
      }

      memcpy(InputBuffer+Remaining,mpa_buf,mpa_buflen);

      mad_stream_buffer(&Stream,InputBuffer,mpa_buflen+Remaining);
      Stream.error=0;
//      fprintf(stderr,"Added %d bytes to mad buffer\n",mpa_buflen);
      mpa_buflen=0;
    }
  }
 }
}

/*  Write out an mpeg audio stream */
void write_out_mpa(uint8_t *buf, int count,void  *priv)
{
  ipack *p = (ipack *) priv;
  u8 payl = buf[8]+9+p->start-1;

  /* If the input buffer is full, then something is wrong with the stream */
  if ((count-payl) > (sizeof(mpa_buf)-mpa_buflen)) {
     fprintf(stderr,"ERROR: Could not sync to a frame in the mpeg audio stream, aborting.\n");
     exit(1);
  }

  memcpy(&mpa_buf[mpa_buflen],buf+payl,count-payl);
  mpa_buflen+=(count-payl);

  p->start = 1;
}

void init_ts2es() {
  ts2es_buf.count=0;

  init_ipack(&ts2es_buf.p, IPACKS,write_out_mpa, 0);

  ts2es_buf.p.fd = STDOUT_FILENO;
  ts2es_buf.p.data = (void *)&ts2es_buf.p;
}

/* Based on the ts2es function from mpegtools 
 * modified to read from a RTP socket instead of a file descriptor */
void myts2es()
{
  int i;
  uint16_t pid;

  for( i = 0; i < ts2es_buf.count; i+= TS_SIZE){
    uint8_t off = 0;

    if ( ts2es_buf.count - i < TS_SIZE) break;

    pid = get_pid(ts2es_buf.buf+i+1);
    if (!(ts2es_buf.buf[3+i]&0x10)) // no payload?
      continue;
    if (pid != ts2es_buf.pidv){
      continue;
    }

    if ( ts2es_buf.buf[3+i] & 0x20) {  // adaptation field?
      off = ts2es_buf.buf[4+i] + 1;
    }

    if ( ts2es_buf.buf[1+i]&0x40) {
      if (ts2es_buf.p.plength == MMAX_PLENGTH-6){
        ts2es_buf.p.plength = ts2es_buf.p.found-6;
        ts2es_buf.p.found = 0;
        send_ipack(&ts2es_buf.p);
        reset_ipack(&ts2es_buf.p);
      }
    }

    instant_repack(ts2es_buf.buf+4+off+i, TS_SIZE-4-off, &ts2es_buf.p);
  }
}

void process_args(int argc,char** argv) {
  int i=1;

  while (i<argc) {
    if (strcmp(argv[i],"-t")==0) {
      i++;
      if (i==argc) {
        fprintf(stderr,"Argument needed for -t\n");
        exit(1);
      }
      secs=atoi(argv[i]);
    } else if (strcmp(argv[i],"-o")==0) {
      i++;
      if (i==argc) {
        fprintf(stderr,"Argument needed for -o\n");
        exit(1);
      }
      user_outfile=argv[i];
    } else if (strcmp(argv[i],"-ao")==0) {
      i++;
      if (i==argc) {
        fprintf(stderr,"Argument needed for -ao\n");
        exit(1);
      }
      if (strcmp(argv[i],"oss")==0) {
        output_type=AUDIO_OSS;
      } else if (strcmp(argv[i],"mpa")==0) {
        output_type=AUDIO_MPA;
      } else if (strcmp(argv[i],"pcm")==0) {
        output_type=AUDIO_PCM;
      } else {
        fprintf(stderr,"ERROR: Unsupported audio type %s\n",argv[i]);
        exit(1);
      }
    } else {
      if (i==(argc-1)) {
        ts2es_buf.pidv=atoi(argv[i]);
        fprintf(stderr,"Using PID %d\n",ts2es_buf.pidv);
      } else {
        fprintf(stderr,"ERROR: Unsupported option %s\n",argv[i]); 
        exit(1);
      }
    }
    i++;
  }
}


void mad_process() {
int i;
static struct dither d0, d1;

   while ((Stream.buffer!=NULL) && (Stream.error!=MAD_ERROR_BUFLEN)) {
    if(mad_frame_decode(&Frame,&Stream)) {
      if(MAD_RECOVERABLE(Stream.error))
      {
        fprintf(stderr,"%s: recoverable frame level error (%s)\n",ProgName,mad_stream_errorstr(&Stream));
        fflush(stderr);
        continue;
      } else if(Stream.error==MAD_ERROR_BUFLEN) { 
        continue;
      } else {
        fprintf(stderr,"%s: unrecoverable frame level error (%s).\n",ProgName,mad_stream_errorstr(&Stream));
        Status=1;
        break;
      }
    }
    mad_timer_add(&Timer,Frame.header.duration);
        
    mad_synth_frame(&Synth,&Frame);

    for(i=0;i<Synth.pcm.length;i++)
    {
      unsigned short  Sample;

      /* Left channel */
      Sample=scale(Synth.pcm.samples[0][i],&d0);
      *(OutputPtr++)=Sample&0xff;
      *(OutputPtr++)=Sample>>8;

      /* Right channel. If the decoded stream is monophonic then
       * the right output channel is the same as the left one.
       */
      if(MAD_NCHANNELS(&Frame.header)==2)
        Sample=scale(Synth.pcm.samples[1][i],&d1);
      *(OutputPtr++)=Sample&0xff;
      *(OutputPtr++)=Sample>>8;

      /* Flush the buffer if it is full. */
      if(OutputPtr==OutputBufferEnd)
      {
        if(write(sound,OutputBuffer,OUTPUT_BUFFER_SIZE)!=OUTPUT_BUFFER_SIZE)
        {
          fprintf(stderr,"%s: PCM write error (%s).\n",ProgName,strerror(errno));
          Status=2;
          break;
        }
        OutputPtr=OutputBuffer;
      }
    }
   }
}

static void SignalHandler(int signum) {
  if (signum != SIGPIPE) {
    Interrupted=signum;
  }
  signal(signum,SignalHandler);
}

int main(int argc, char *argv[]) {
  struct sockaddr_in si;
  int socketIn;
  struct rtpheader rh;
  char *ip;
  int port;
  unsigned short seq;

  fprintf(stderr,"\nrtptsaudio version 0.2, Copyright (C) 2002 Dave Chapman\n");
  fprintf(stderr,"rtptsaudio comes with ABSOLUTELY NO WARRANTY;\n");
  fprintf(stderr,"This is free software, and you are welcome to redistribute it\n");
  fprintf(stderr,"under certain conditions;\n");
  fprintf(stderr,"See http://www.gnu.org/copyleft/gpl.txt for details.\n\n");

  ip = "224.0.1.2";
  port = 5004;

  if (argc<2) {
    fprintf(stderr,"Usage: rtptsaudio [-ao audiotype] [-o filename] [-t secs] pid\n");
    fprintf(stderr,"\nOptions: -ao oss    Linux Open Sound System output (default)\n");
    fprintf(stderr,"             mpa    Unprocessed MPEG Audio stream to stdout\n");
    fprintf(stderr,"             raw    Raw PCM data (16 bit Little-Endian Stereo) to stdout\n");
    fprintf(stderr,"         -o  file   Output filename or audio device\n");
    fprintf(stderr,"         -t  secs   Number of seconds to receive before quitting\n");
    fprintf(stderr,"\n");
    return(-1);
  }

  ts2es_buf.pidv=0;
  process_args(argc,argv);

  if (ts2es_buf.pidv==0) {
    fprintf(stderr,"Invalid PID \"%s\"\n",argv[1]);
    return(-1);
  }
     
  if (signal(SIGHUP, SignalHandler) == SIG_IGN) signal(SIGHUP, SIG_IGN);
  if (signal(SIGINT, SignalHandler) == SIG_IGN) signal(SIGINT, SIG_IGN);
  if (signal(SIGTERM, SignalHandler) == SIG_IGN) signal(SIGTERM, SIG_IGN);
  if (signal(SIGALRM, SignalHandler) == SIG_IGN) signal(SIGALRM, SIG_IGN);

  fprintf(stderr,"rtptsaudio: Listening for RTP stream on %s:%d\n",ip,port);

  socketIn  = makeclientsocket(ip,port,2,&si);

  init_ts2es();

  if (output_type!=AUDIO_MPA) {
    mad_stream_init(&Stream);
    mad_frame_init(&Frame);
    mad_synth_init(&Synth);
    mad_timer_reset(&Timer);
  }

  getrtp2(socketIn,&rh, (char**) &ts2es_buf.buf,&ts2es_buf.count);
  seq=rh.b.sequence;

  if (secs > 0) alarm(secs);

  Interrupted=0;
  while (!Interrupted) {
   getrtp2(socketIn,&rh, (char**) &ts2es_buf.buf,&ts2es_buf.count);
   seq++;
   if (seq!=rh.b.sequence) {
     fprintf(stderr,"rtptsaudio: NETWORK CONGESTION - expected %d, received %d\n",seq,rh.b.sequence);
     seq=rh.b.sequence;
   }
   myts2es();
   output_mpa_frames();

   if (output_type!=AUDIO_MPA) {
     mad_process();
   }
  }

  fprintf(stderr,"rtptsaudio: Received signal %d, closing cleanly.\n",Interrupted);
  if ((sound!=1) && (sound!=0)) close(sound);

  close(socketIn);
  return(0);
}
