#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>

#include <ost/dmx.h>
#include <ost/sec.h>
#include <ost/frontend.h>
#include <ost/frontend.h>

#define slof (11700*1000UL)
#define lof1 (9750*1000UL)
#define lof2 (10600*1000UL)


int OSTSelftest(int fd)
{
    int ans;

    if ( (ans = ioctl(fd,FE_SELFTEST,0) < 0)){
        perror("FE SELF TEST: ");
        return -1;
    }

    return 0;
}

int OSTSetPowerState(int fd, uint32_t state)
{
    int ans;

    if ( (ans = ioctl(fd,FE_SET_POWER_STATE,state) < 0)){
        perror("OST SET POWER STATE: ");
        return -1;
    }

    return 0;
}

int OSTGetPowerState(int fd, uint32_t *state)
{
    int ans;

    if ( (ans = ioctl(fd,FE_GET_POWER_STATE,state) < 0)){
        perror("OST GET POWER STATE: ");
        return -1;
    }

    switch(*state){
    case FE_POWER_ON:
        fprintf(stderr,"POWER ON (%d)\n",*state);
        break;
    case FE_POWER_STANDBY:
        fprintf(stderr,"POWER STANDBY (%d)\n",*state);
        break;
    case FE_POWER_SUSPEND:
        fprintf(stderr,"POWER SUSPEND (%d)\n",*state);
        break;
    case FE_POWER_OFF:
        fprintf(stderr,"POWER OFF (%d)\n",*state);
        break;
    default:
        fprintf(stderr,"unknown (%d)\n",*state);
        break;
    }

    return 0;
}

int SecGetStatus (int fd, struct secStatus *state)
{
    int ans;

    if ( (ans = ioctl(fd,SEC_GET_STATUS, state) < 0)){
        perror("SEC GET STATUS: ");
        return -1;
    }

    switch (state->busMode){
    case SEC_BUS_IDLE:
        fprintf(stderr,"SEC BUS MODE:  IDLE (%d)\n",state->busMode);
        break;
    case SEC_BUS_BUSY:
        fprintf(stderr,"SEC BUS MODE:  BUSY (%d)\n",state->busMode);
        break;
    case SEC_BUS_OFF:
        fprintf(stderr,"SEC BUS MODE:  OFF  (%d)\n",state->busMode);
        break;
    case SEC_BUS_OVERLOAD:
        fprintf(stderr,"SEC BUS MODE:  OVERLOAD (%d)\n",state->busMode);
        break;
    default:
        fprintf(stderr,"SEC BUS MODE:  unknown  (%d)\n",state->busMode);
        break;
    }

    switch (state->selVolt){
    case SEC_VOLTAGE_OFF:
        fprintf(stderr,"SEC VOLTAGE:  OFF (%d)\n",state->selVolt);
        break;
    case SEC_VOLTAGE_LT:
        fprintf(stderr,"SEC VOLTAGE:  LT  (%d)\n",state->selVolt);
        break;
    case SEC_VOLTAGE_13:
        fprintf(stderr,"SEC VOLTAGE:  13  (%d)\n",state->selVolt);
        break;
    case SEC_VOLTAGE_13_5:
        fprintf(stderr,"SEC VOLTAGE:  13.5 (%d)\n",state->selVolt);
        break;
    case SEC_VOLTAGE_18:
        fprintf(stderr,"SEC VOLTAGE:  18 (%d)\n",state->selVolt);
        break;
    case SEC_VOLTAGE_18_5:
        fprintf(stderr,"SEC VOLTAGE:  18.5 (%d)\n",state->selVolt);
        break;
    default:
        fprintf(stderr,"SEC VOLTAGE:  unknown (%d)\n",state->selVolt);
        break;
    }

    fprintf(stderr,"SEC CONT TONE: %s\n", (state->contTone == SEC_TONE_ON ? "ON" : "OFF"));
    return 0;
}

int tune_it(int fd_frontend, int fd_sec, unsigned int freq, unsigned int srate, char pol, int tone, SpectralInversion specInv, unsigned int diseqc) {
  int i,res;
  int32_t strength;
  FrontendStatus festatus;
  FrontendEvent event;
  FrontendParameters feparams;
  secVoltage voltage;
  struct pollfd pfd[1];
  struct secStatus sec_state;

//  OSTSelftest(fd_frontend);
//  OSTSetPowerState(fd_frontend, FE_POWER_ON);
//  OSTGetPowerState(fd_frontend, &festatus);

  if (freq > 100000000) {
    fprintf(stderr,"tuning DVB-T to %d\n",freq);
    feparams.Frequency=freq;
    feparams.u.ofdm.bandWidth=BANDWIDTH_8_MHZ; // WAS: 8
    feparams.u.ofdm.HP_CodeRate=FEC_2_3;
    feparams.u.ofdm.LP_CodeRate=FEC_1_2;
    feparams.u.ofdm.Constellation=QAM_64;  // WAS: 16
    feparams.u.ofdm.TransmissionMode=TRANSMISSION_MODE_2K;
    feparams.u.ofdm.guardInterval=GUARD_INTERVAL_1_32;
    feparams.u.ofdm.HierarchyInformation=HIERARCHY_NONE;
  } else {
    if ((pol=='h') || (pol=='H')) {
      voltage = SEC_VOLTAGE_18;
    } else {
      voltage = SEC_VOLTAGE_13;
    }
    if (ioctl(fd_sec,SEC_SET_VOLTAGE,voltage) < 0) {
       perror("ERROR setting voltage\n");
    }

    if (freq > 2200000) {
    // this must be an absolute frequency
      if (freq < slof) {
        feparams.Frequency=(freq-lof1);
        if (tone < 0) tone = SEC_TONE_OFF;
    } else {
        feparams.Frequency=(freq-lof2);
        if (tone < 0) tone = SEC_TONE_ON;
    }
  } else {
    // this is an L-Band frequency
    feparams.Frequency=freq;
  }
    feparams.Inversion=specInv;
    feparams.u.qpsk.SymbolRate=srate;
    feparams.u.qpsk.FEC_inner=FEC_AUTO;
  
    if (ioctl(fd_sec,SEC_SET_TONE,tone) < 0) {
       perror("ERROR setting tone\n");
    }

    if (diseqc > 0) {
      struct secCommand scmd;
      struct secCmdSequence scmds;

      scmds.continuousTone = tone;
      scmds.voltage = voltage;
      /*
      scmds.miniCommand = toneBurst ? SEC_MINI_B : SEC_MINI_A;
      */
      scmds.miniCommand = SEC_MINI_NONE;

      scmd.type = 0;
      scmds.numCommands = 1;
      scmds.commands = &scmd;

      scmd.u.diseqc.addr = 0x10;
      scmd.u.diseqc.cmd = 0x38;
      scmd.u.diseqc.numParams = 1;
      scmd.u.diseqc.params[0] = 0xf0 | 
                                (((diseqc - 1) << 2) & 0x0c) |
                                (voltage==SEC_VOLTAGE_18 ? 0x02 : 0) |
                                (tone==SEC_TONE_ON ? 0x01 : 0);

      if (ioctl(fd_sec,SEC_SEND_SEQUENCE,&scmds) < 0) {
        perror("Error sending DisEqC");
        return -1;
      }
    }

    fprintf(stderr,"tuning DVB-S to L-Band:%d, Pol:%c Srate=%d, 22kHz=%s\n",feparams.Frequency,pol,srate,tone == SEC_TONE_ON ? "on" : "off");
    usleep(100000);
  }

  if (fd_sec) SecGetStatus(fd_sec, &sec_state);

  i = 0; res = -1;
  while ((i < 3) && (res < 0)) {

    if (ioctl(fd_frontend,FE_SET_FRONTEND,&feparams) < 0) {
      perror("ERROR tuning channel\n");
      return -1;
    }

    pfd[0].fd = fd_frontend;
    pfd[0].events = POLLIN;

    if (poll(pfd,1,10000)){
        if (pfd[0].revents & POLLIN){
            fprintf(stderr,"Getting frontend event\n");
            if ( ioctl(fd_frontend, FE_GET_EVENT, &event) == -EBUFFEROVERFLOW){
                perror("FE_GET_EVENT");
                return -1;
            }
            fprintf(stderr,"Received ");
            switch(event.type){
            case FE_UNEXPECTED_EV:
                fprintf(stderr,"unexpected event\n");
                res = -1;
          break;
            case FE_FAILURE_EV:
                fprintf(stderr,"failure event\n");
                res = -1;
          break;
            case FE_COMPLETION_EV:
                fprintf(stderr,"completion event\n");
                res = 0;
          break;
            }
        }
  i++;
    }
  }

  if (res > 0)
    switch (event.type) {
       case FE_UNEXPECTED_EV: fprintf(stderr,"FE_UNEXPECTED_EV\n");
                                break;
       case FE_COMPLETION_EV: fprintf(stderr,"FE_COMPLETION_EV\n");
                                break;
       case FE_FAILURE_EV: fprintf(stderr,"FE_FAILURE_EV\n");
                                break;
    }

    if (event.type == FE_COMPLETION_EV) {
      if (freq > 100000000) {
        fprintf(stderr,"Event:  Frequency: %d\n",event.u.completionEvent.Frequency);
      } else {
        fprintf(stderr,"Event:  Frequency: %d\n",(unsigned int)((event.u.completionEvent.Frequency)+(tone==SEC_TONE_OFF ? lof1 : lof2)));
        fprintf(stderr,"        SymbolRate: %d\n",event.u.completionEvent.u.qpsk.SymbolRate);
        fprintf(stderr,"        FEC_inner:  %d\n",event.u.completionEvent.u.qpsk.FEC_inner);
        fprintf(stderr,"\n");
      }

      strength=0;
      ioctl(fd_frontend,FE_READ_BER,&strength);
      fprintf(stderr,"Bit error rate: %d\n",strength);

      strength=0;
      ioctl(fd_frontend,FE_READ_SIGNAL_STRENGTH,&strength);
      fprintf(stderr,"Signal strength: %d\n",strength);

      strength=0;
      ioctl(fd_frontend,FE_READ_SNR,&strength);
      fprintf(stderr,"SNR: %d\n",strength);

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
    } else {
#if 0
    FrontendInfo info;
    if ( (res = ioctl(fd_frontend,FE_GET_INFO, &info) < 0)){
       perror("FE_GET_INFO: ");
       return -1;
    }

    fprintf(stderr,"min Frequency   : %d\n", info.minFrequency);
    fprintf(stderr,"max Frequency   : %d\n", info.maxFrequency);
    fprintf(stderr,"min Symbol Rate : %d\n", info.minSymbolRate);
    fprintf(stderr,"max Symbol Rate : %d\n", info.maxSymbolRate);
    fprintf(stderr,"Hardware Type   : %d\n", info.hwType);
    fprintf(stderr,"Hardware Version: %d\n", info.hwVersion);
#endif
    fprintf(stderr,"Not able to lock to the signal on the given frequency\n");
    return -1;
  }
  return 0;
}

