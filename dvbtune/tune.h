#ifndef _TUNE_H
#define _TUNE_H

int tune_it(int fd_frontend, int fd_sec, unsigned int freq, unsigned int srate, char pol, int tone, SpectralInversion specInv, unsigned int diseqc);

#endif
