/* 

pesdump - a program to dump PES streams from a DVB card.
(C) Copyright Dave Chapman June 2001

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

#include <sys/ioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/poll.h>

#include <ost/dmx.h>

void set_filt(int fd,uint16_t tt_pid, dmxPesType_t t)
{
	size_t bytesRead;
	struct dmxPesFilterParams pesFilterParams;

	pesFilterParams.pid     = tt_pid;
	pesFilterParams.input   = DMX_IN_FRONTEND;
	pesFilterParams.output  = DMX_OUT_TAP;
        pesFilterParams.pesType = t;
	pesFilterParams.flags   = DMX_IMMEDIATE_START;

	if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  
		perror("DMX SET PES FILTER:");
}

main(int argc, char **argv)
{
  struct pollfd pfd;
  unsigned short pid;
  int i,n;
  int pid1,pid2;
  unsigned char buf[60000]; /* data buffer */

  int fd,fd_dvr,filefd,fd1,fd2;
  int skip;

  if (argc!=2) {
    fprintf(stderr,"USAGE: pesdump PID\n\nPES stream will be written to stdout\n\n");
    return -1;
  }

  pid1=atoi(argv[1]);
  if((fd1 = open("/dev/ost/demux",O_RDWR)) < 0){
    perror("DEMUX DEVICE 1: ");
    return -1;
  }

  set_filt(fd1,pid1,DMX_PES_OTHER); 

  for (;;) {
    n = read(fd1,buf,sizeof(buf));
    fwrite(buf,1,n,stdout);
  }
  close(fd1);
}
