/* A simple filter (stdin -> stdout) to extract a single stream from a
   multiplexed TS.  Specify the PID on the command-line 

   Updated 29th January 2003 - Added some error checking and reporting.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
  int pid,n;
  unsigned int i=0;
  unsigned int j=0;
  unsigned char buf[188];
  unsigned char my_cc;
  int errors=0;

  pid=atoi(argv[1]);

  fprintf(stderr,"filtering PID %d\n",pid);

  n=fread(buf,1,188,stdin);
  my_cc=buf[3]&0x0f;
  i++;
  while (n==188) {
    if (buf[0]!=0x47) {
      // TO DO: Re-sync.
      fprintf(stderr,"FATAL ERROR IN STREAM AT PACKET %d\n",i);
      exit;
    }
    if (pid==(((buf[1] & 0x1f) << 8) | buf[2])) {
      if (my_cc!=(buf[3]&0x0f)) {
        fprintf(stderr,"Output packet %d: packet incontinuity - expected %02x, found %02x\n",j,my_cc,buf[3]&0x0f);
        my_cc=buf[3]&0x0f;
        errors++;
      }
      n=fwrite(buf,1,188,stdout);
      if (n==188) {
        j++;
      } else {
        fprintf(stderr,"FATAL ERROR - CAN NOT WRITE PACKET %d\n",i);
        exit;
      }
      if (my_cc==0x0f) {
        my_cc=0;
      } else {
        my_cc++;
      }
    }
    n=fread(buf,1,188,stdin);
  }
  fprintf(stderr,"Read %d packets, wrote %d.\n",i,j);
  fprintf(stderr,"%d incontinuity errors.\n",errors);
  return(0);
}
