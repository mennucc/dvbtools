/* A simple filter (stdin -> stdout) to extract a single stream from a
   multiplexed TS.  Specify the PID on the command-line */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
  int pid,n;
  unsigned char buf[188];

  pid=atoi(argv[1]);

  fprintf(stderr,"filtering PID %d\n",pid);

  for (;;) {
    n=fread(buf,1,188,stdin);
    if (n==188) {
      if (pid==(((buf[1] & 0x1f) << 8) | buf[2])) {
        fwrite(buf,1,188,stdout);
      }
    }
  }
  return(0);
}
