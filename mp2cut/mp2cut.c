#include <stdio.h>

int bitrates[16]={0,32000,48000,56000,64000,80000,96000,112000,128000,160000,192000,224000,256000,320000,384000,0};
int freqs[4]={44100,48000,32000,0};

int main(int argc, char **argv) {

  unsigned char buf[10000];
  int n;
  int skip=0;
  int found=0;
  unsigned char layer,id,prot,bitrate,freq,priv,padding;
  int framesize;
  long int fc=0;
  long int fw=0;
  int ok=1;
  int cp;

  long int start=-1;
  long int end=-1;
  int test=0;

  n=1;
  while (n < argc) {
    if (!strcmp(argv[n],"-t")) {
      test=1;
      fprintf(stderr,"Test mode\n");
      n++;
    } else if (!strcmp(argv[n],"-s")) {
      n++;
      if (n < argc) start=atoi(argv[n]);
      n++;
    } else if (!strcmp(argv[n],"-e")) {
      n++;
      if (n < argc) end=atoi(argv[n]);
      n++;
    } else {
      n++;
    }
  }
  /* Find first frame */

  fprintf(stderr,"Start: %d\n",start);
  fprintf(stderr,"End: %d\n",end);

  while (!found) {
    n=fread(buf,1,1,stdin);
    while (buf[0]!=0xff) {
      skip++;
      n=fread(buf,1,1,stdin);
    }
    n=fread(&buf[1],1,1,stdin);
    if ((buf[1]&0xf0)==0xf0) {
      found=1;
    } else {
      skip++;
    }
  }
  
  fprintf(stderr,"Skipping first %d bytes of file\n",skip);

  fread(&buf[2],1,2,stdin);
  fprintf(stderr,"Frame header: %02x%02x%02x%02x\n",buf[0],buf[1],buf[2],buf[3]);
    id=(buf[1]&0x08);
    layer=(buf[1]&0x06);
    if ((id!=0x08) || (layer!=0x04)) {
      fprintf(stderr,"File isn't MPEG-1 Layer 2 - id: %02x layer: %02x - aborting\n",id,layer);
      return -1;
    }

    bitrate=(buf[2]&0xf0)>>4;
    freq=(buf[2]&12)>>2;
    fprintf(stderr,"File type: MPEG-1 Layer 2\n");
    fprintf(stderr,"Bitrate: %d bits per second\n",bitrates[bitrate]);
    fprintf(stderr,"Frequency: %dKHz\n",freqs[freq]);
    framesize=(144*bitrates[bitrate])/freqs[freq];
    fprintf(stderr,"Framesize: %d or %d bytes\n",framesize,framesize+1);

  while (ok) {
    padding=(buf[2]&2)>>1;

    n=fread(&buf[4],1,framesize+padding-4,stdin);

    if (n==framesize+padding-4) {
      cp=(1-test);
      if ((start!=-1) && (fc < start)) cp=0;
      if ((end!=-1) && (fc > end)) {
        cp=0;
        ok=0;
      }
      if (ok) {
        if (cp) { 
         fwrite(buf,1,4+n,stdout);
         fw++;
        }
        fc++;
        n=fread(buf,1,4,stdin);
        ok=(n==4);
     }
    } else {
      fprintf(stderr,"Skipping last %d bytes of file\n",n);
      ok=0;
    }
  }

  fprintf(stderr,"Total frames read: %d (%.1f seconds)\n",fc,fc*0.024);
  fprintf(stderr,"Total frames copied: %d (%.1f seconds)\n",fw,fw*0.024);
}
