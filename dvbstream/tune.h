#ifndef _TUNE_H
#define _TUNE_H

int open_fe(int* fd_frontend,int* fd_sec);
int tune_it(int fd_frontend, int fd_sec, unsigned long freq, unsigned long srate, char pol);

#endif
