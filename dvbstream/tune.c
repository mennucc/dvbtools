#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>

#include <ost/dmx.h>
#include <ost/sec.h>
#include <ost/frontend.h>

#define slof (11700*1000UL)
#define lof1 (9750*1000UL)
#define lof2 (10600*1000UL)

int tune_it(int fd_frontend, int fd_sec, unsigned long freq, unsigned long srate, char pol) {

  int i,res;
  int32_t strength;
  FrontendStatus festatus;
  FrontendEvent event;
  FrontendParameters feparams;
  secToneMode tone;
  secVoltage voltage;

  if (freq > 100000000) {
    fprintf(stderr,"tuning DVB-T to %u\n",freq);
    feparams.Frequency=freq;
    feparams.u.ofdm.bandWidth=BANDWIDTH_8_MHZ; // WAS: 8
    feparams.u.ofdm.HP_CodeRate=FEC_2_3;
    feparams.u.ofdm.LP_CodeRate=FEC_1_2;
    feparams.u.ofdm.Constellation=QAM_64;  // WAS: 16
    feparams.u.ofdm.TransmissionMode=TRANSMISSION_MODE_2K;
    feparams.u.ofdm.guardInterval=GUARD_INTERVAL_1_32;
    feparams.u.ofdm.HierarchyInformation=HIERARCHY_NONE;
  } else {
    fprintf(stderr,"tuning DVB-S to %d%c %d\n",freq,pol,srate);
    if (freq < slof) {
      feparams.Frequency=(freq-lof1);
      tone = SEC_TONE_OFF;
    } else {
      feparams.Frequency=(freq-lof2);
      tone = SEC_TONE_ON;
    }
    feparams.Inversion=INVERSION_AUTO;

    if ((pol=='h') || (pol=='H')) {
      voltage = SEC_VOLTAGE_18;
    } else {
     voltage = SEC_VOLTAGE_13;
    }

    feparams.u.qpsk.SymbolRate=srate;
    feparams.u.qpsk.FEC_inner=FEC_AUTO;
  
    if (ioctl(fd_sec,SEC_SET_TONE,tone) < 0) {
       perror("ERROR setting tone\n");
    }
    
    if (ioctl(fd_sec,SEC_SET_VOLTAGE,voltage) < 0) {
       perror("ERROR setting voltage\n");
    }
    usleep(200000);
  }

  if (ioctl(fd_frontend,FE_SET_FRONTEND,&feparams) < 0) {
     perror("ERROR tuning channel\n");
  } else {
     fprintf(stderr,"Channel tuned\n");
  }

//  usleep(5000000);
  i=10;

  res = ioctl(fd_frontend, FE_GET_EVENT, &event);
  while ((i<10) && (res < 0)) {
    res = ioctl(fd_frontend, FE_GET_EVENT, &event);
    i--;
  }

  if (res < 0)
    perror("qpsk get event");
  else
    switch (event.type) {
       case FE_UNEXPECTED_EV: fprintf(stderr,"FE_UNEXPECTED_EV\n");
                                break;
       case FE_COMPLETION_EV: fprintf(stderr,"FE_COMPLETION_EV\n");
                                break;
       case FE_FAILURE_EV: fprintf(stderr,"FE_FAILURE_EV\n");
                                break;
   }

  if (freq > 100000000) {
    fprintf(stderr,"Event:  iFrequency: %ld\n",event.u.completionEvent.Frequency);
  } else {
    fprintf(stderr,"Event:  iFrequency: %ld\n",(event.u.completionEvent.Frequency)+(tone==SEC_TONE_OFF ? lof1 : lof2));
    fprintf(stderr,"        SymbolRate: %ld\n",event.u.completionEvent.u.qpsk.SymbolRate);
    fprintf(stderr,"        FEC_inner:  %d\n",event.u.completionEvent.u.qpsk.FEC_inner);
    fprintf(stderr,"\n");
  }

  strength=0;
  ioctl(fd_frontend,FE_READ_BER,&strength);
  fprintf(stderr,"Bit error rate: %ld\n",strength);

  strength=0;
  ioctl(fd_frontend,FE_READ_SIGNAL_STRENGTH,&strength);
  fprintf(stderr,"Signal strength: %ld\n",strength);

  strength=0;
  ioctl(fd_frontend,FE_READ_SNR,&strength);
  fprintf(stderr,"SNR: %ld\n",strength);

  festatus=0;
  ioctl(fd_frontend,FE_READ_STATUS,&festatus);

  fprintf(stderr,"FE_STATUS:");
  if (festatus & FE_HAS_POWER) fprintf(stderr," FE_HAS_POWER");
  if (festatus & FE_HAS_SIGNAL) fprintf(stderr," FE_HAS_SIGNAL");
  if (festatus & FE_SPECTRUM_INV) fprintf(stderr," FE_SPECTRUM_INV");
  if (festatus & FE_HAS_LOCK) fprintf(stderr," FE_HAS_LOCK");
  if (festatus & FE_HAS_CARRIER) fprintf(stderr," FE_HAS_CARRIER");
  if (festatus & FE_HAS_VITERBI) fprintf(stderr," FE_HAS_VITERBI");
  if (festatus & FE_HAS_SYNC) fprintf(stderr," FE_HAS_SYNC");
  if (festatus & FE_TUNER_HAS_LOCK) fprintf(stderr," FE_TUNER_HAS_LOCK");
  fprintf(stderr,"\n");
}

