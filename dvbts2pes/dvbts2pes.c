/*
   dvbts2pes - a simple filter to extract a single PES from a TS

   File: dvbts2pes.c

   Copyright (C) Dave Chapman 2002
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html
*/

/* Structure of Transport Stream (from ISO/IEC 13818-1):

transport_packet() {
  sync_byte                                   8    bslbf       0
  transport_error_indicator                   1    bslbf       1
  payload_unit_start_indicator                1    bslbf       1
  transport_priority                          1    bslbf       1
  PID                                         13   uimsbf      1,2
  transport_scrambling_control                2    bslbf       3
  adaption_field_control                      2    bslbf       3
  continuity_counter                          4    uimsbf      3
  if (adaption_field_control=='10' || adaption_field_control=='11'){
      adaption_field()
  }
  if (adaption_field_control=='01' || adaption_field_control=='11'){
      for (i=0;i<N;i++){
        data_byte                             8    bslbf
      }
  }
}

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef enum {
  TS_WAITING,
  TS_ERROR,
  TS_IN_PAYLOAD
} ts_status_t;

int check_pes(unsigned char* buf,int n) {
  int i=0;
  int stream_id;
  int PES_packet_length;
 

  if ((buf[0]!=0) || (buf[1]!=0) || (buf[2]!=1)) {
    fprintf(stderr,"PES ERROR: does not start with 0x000001\n");
    return(0);
//  } else {
//    fprintf(stderr,"PES starts with 0x000001\n");
  }
  i=3;
  // stream_id: e0=video, c0=audio, bd=DVB subtitles, AC3 etc
  stream_id=buf[i++];
  PES_packet_length=(buf[i]<<8)|buf[i+1]; i+=2;

  if ((stream_id&0xe0)==0xe0) {
    // Video stream - PES_packet_length must be equal to zero
    if (PES_packet_length!=0) {
      fprintf(stderr,"VIDEO PES STREAM: PES_packet_length=%d (must be zero)\n",PES_packet_length);
      return(0);
    }
  } else {
    if (PES_packet_length+6!=n) {
      fprintf(stderr,"NON-VIDEO PES: n=%d,PES_packet_length=%d\n",n,PES_packet_length);
      return(0);
    }
  }
  return(1);
}

int main(int argc, char** argv) {
  unsigned char buf[188];
  unsigned char pesbuf[165536];
  int peslength;
  unsigned short pid;
  int n,c;
  ts_status_t ts_status;
  int i;
  int packet;
  int counter;
  int adaption_field_control,continuity_counter;
  int adaption_field_length;
  int max_pes=0;
  int min_pes=9999999;
  int pes_written=0;
  int pes_dropped=0;
  int ts_dropped=0;

  if (argc==2) {
    pid=atoi(argv[1]);
  } else {
    fprintf(stderr,"dvbts2pes: Usage - dvbts2pes PID < file.ts > file.pes\n");
    exit(0);
  }

  packet=0;
  peslength=-1;
  counter=-1;
  ts_status=TS_WAITING;
  for (;;) {
    c=0;
    while (c<188) {
      n=read(0,&buf[c],188-c);
      if (n==0) {
        fprintf(stderr,"END OF STREAM\n");
        fprintf(stderr,"Processed %d TS packets (%d bytes).\n",packet,188*packet);
        fprintf(stderr,"Number of TS packets missing:  %d\n",ts_dropped);
        fprintf(stderr,"Minimum PES packet size:       %d bytes\n",min_pes);
	fprintf(stderr,"Maximum PES packet size:       %d bytes\n",max_pes);
        fprintf(stderr,"Number of PES packets written: %d\n",pes_written);
        fprintf(stderr,"Number of PES packets dropped: %d\n",pes_dropped);
        exit(0);
      }
      c+=n;
    }
//    fprintf(stderr,"Read %d bytes\n",c);

    if ( (((buf[1]&0x1f)<<8) | buf[2]) == pid) {
      continuity_counter=buf[3]&0x0f;
      adaption_field_control=(buf[3]&0x30)>>4;

      /* Firstly, check the integrity of the stream */
      if (counter==-1) {
        counter=continuity_counter;
      } else {
        counter++; counter%=16;
      }

      if (counter!=continuity_counter) {
        n=0;
        c=counter;
        while (c!=continuity_counter) {
          c++; c%=16;
          n++;
        }
        n=(continuity_counter+16-counter)%16;
           
        fprintf(stderr,"TS: missing %d packet(s), packet=%d, expecting %02x, received %02x\n",n,packet,counter,continuity_counter);
        ts_dropped+=n;
        counter=continuity_counter;
      }

      // Check payload start indicator.
      if (buf[1]&0x40) {
//        fprintf(stderr,"%d: payload start\n",packet);
//        fprintf(stderr,"previous peslength=%d\n",peslength);
        if (ts_status==TS_IN_PAYLOAD) {
          if (check_pes(pesbuf,peslength)) {
            c=0;
            while (c<peslength) {
              n=write(1,&pesbuf[c],peslength-c);
              c+=n;
            }
//          fprintf(stderr,"written PES packet - %d bytes\n",peslength);
            if (peslength > max_pes) { max_pes=peslength; }
            if (peslength < min_pes) { min_pes=peslength; }
            pes_written++;
          } else {
            pes_dropped++;
          }
        } else {
          ts_status=TS_IN_PAYLOAD;
        }
        peslength=0;
      }

      if (ts_status==TS_IN_PAYLOAD) {
//        fprintf(stderr,"processing packet\n");
        i=4;
        if ((adaption_field_control==2) || (adaption_field_control==3)) {
          // Adaption field!!!
          adaption_field_length=buf[i++];
          i+=adaption_field_length;
//          fprintf(stderr,"Adaption field length=%d\n",adaption_field_length);
        }
        if ((adaption_field_control==1) || (adaption_field_control==3)) {
          // Data
//          fprintf(stderr,"(before)peslength=%d\n",peslength);
//          fprintf(stderr,"copying %d bytes to pesbuf\n",188-i);
          if ((peslength+(188-i)) > sizeof(pesbuf)) {
            fprintf(stderr,"ERROR: TS PACKET %d: PES packet > %d bytes, skipping end\n",packet,sizeof(pesbuf));
          } else {
            memcpy(&pesbuf[peslength],&buf[i],188-i);
          }
          peslength+=(188-i);
//          fprintf(stderr,"(after)peslength=%d\n",peslength);
        }

//        fprintf(stderr,"packet %d, counter=%d, cc=%d, afc=%d\n",packet,counter,continuity_counter,adaption_field_control);
      }
      packet++;
    }
  }
}
