/*
    dvbimage - a program to display still images on a DVB card
    Copyright (C) 2001 Dave Chapman <dave@dchapman.com>

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

#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ost/video.h>
#include <signal.h>

// Refresh every 10 seconds:
#define REFRESH_TIME (10)

// Signal handling code taken from VDR - thanks Klaus!
int Interrupted=0;

void SignalHandler(int signum) {
  if (signum != SIGPIPE)
    Interrupted=signum;
  signal(signum, SignalHandler);
}

int main(int argc,char* argv[]) {
  FILE* f;
  int fd_video;
  char buf[100000];
  int n;
  int i;
  time_t t1,t2;
  struct videoDisplayStillPicture vsp;

  if (argc!=2) {
    fprintf(stderr,"USAGE: dvbimage script.sh\n\n");
    return -1;
  }
   
  if ((fd_video=open("/dev/ost/video0",O_RDWR|O_NONBLOCK)) < 0) {
    perror("fd_video: ");
    return  -1;
  }

  if (signal(SIGHUP,SignalHandler)==SIG_IGN) signal(SIGHUP,SIG_IGN);
  if (signal(SIGINT,SignalHandler)==SIG_IGN) signal(SIGINT,SIG_IGN);
  if (signal(SIGTERM,SignalHandler)==SIG_IGN) signal(SIGTERM,SIG_IGN);
  if (signal(SIGPIPE,SignalHandler)==SIG_IGN) signal(SIGPIPE,SIG_IGN);

  while (!Interrupted) {
    t1=time(NULL);
    fprintf(stderr,"t1=%d\n",t1);
    system(argv[1]);
    f=fopen("/tmp/dvbimage.mpg","r");
    if (f!=NULL) {
      fprintf(stderr,"Reading image\n");
      n=fread(buf,1,sizeof(buf),f);
      fprintf(stderr,"Read %d bytes\n",n);
      fclose(f);

      if (n > 0) {
        vsp.iFrame=buf;
        vsp.size=n;
        if (ioctl(fd_video,VIDEO_STILLPICTURE,&vsp) < 0) {
          perror("VIDEO_STILLPICTURE: ");
        } else {
          fprintf(stderr,"Picture successfully displayed\n");
        }
      }
    }
    t2=time(NULL);
    if ((t2-t1) < REFRESH_TIME) {
      fprintf(stderr,"Sleeping for %d seconds\n",REFRESH_TIME-(t2-t1));
      sleep(REFRESH_TIME-(t2-t1));
    }
  }

  /* We make sure that we close the fd_video device */
  printf("Interrupted - exitting\n");
  close(fd_video);
}
