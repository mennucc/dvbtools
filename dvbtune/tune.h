#ifndef _TUNE_H
#define _TUNE_H

#include <ost/frontend.h>
#include "dvb_defaults.h"

int tune_it(int fd_frontend, int fd_sec, unsigned int freq, unsigned int srate, char pol, int tone, SpectralInversion specInv, unsigned int diseqc,Modulation modulation,CodeRate HP_CodeRate,TransmitMode TransmissionMode,GuardInterval guardInterval, BandWidth bandwidth);

#endif
