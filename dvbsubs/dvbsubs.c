/*
   dvbsubs - a program for decoding DVB subtitles (ETS 300 743)

   File: dvbsubs.c

   Copyright (C) Dave Chapman 2002,2004
  
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


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <sys/poll.h>
#include <ctype.h>
#include <sys/time.h>
#include <errno.h>

#include "dvbsubs.h"
#include "bitmap.h"
#include "pes.h"

#define VERSION "0.3"

page_t page;
region_t regions[MAX_REGIONS];
uint8_t colours[256*3];
uint8_t trans[256];
uint16_t apid;
int y=0;
int x=0;

int acquired=0;
struct timeval start_tv;
uint64_t PTS;
uint64_t first_PTS;

unsigned int curr_obj;
unsigned int curr_reg[64];

uint8_t buf[1024*1024];  // Far too big!
int i=0;
int nibble_flag=0;
int in_scanline=0;

uint64_t video_pts,first_video_pts,audio_pts,first_audio_pts;

void init_data() {
  int i;

  for (i=0;i<MAX_REGIONS;i++) {
    page.regions[i].is_visible=0;
    regions[i].win=-1;
  }
}

void create_region(int region_id,int region_width,int region_height,int region_depth) {

  regions[region_id].win=1;
  regions[region_id].width=region_width;
  regions[region_id].height=region_height;

  memset(regions[region_id].img,15,sizeof(regions[region_id].img));
}

void do_plot(int r,int x, int y, unsigned char pixel) {
  int i;
  if ((y >= 0) && (y < regions[r].height)) {
    i=(y*regions[r].width)+x;
    regions[r].img[i]=pixel;
  } else {
    fprintf(stderr,"plot out of region: x=%d, y=%d - r=%d, height=%d\n",x,y,r,regions[r].height);
  }
}

void plot(int r,int run_length, unsigned char pixel) {
  int x2=x+run_length;

//  fprintf(stderr,"plot: x=%d,y=%d,length=%d,pixel=%d\n",x,y,run_length,pixel);
  while (x < x2) {
    do_plot(r,x,y,pixel);
    x++;
  }
}

unsigned char next_nibble () {
  unsigned char x;

  if (nibble_flag==0) {
    x=(buf[i]&0xf0)>>4;
    nibble_flag=1;
  } else {
    x=(buf[i++]&0x0f);
    nibble_flag=0;
  }
  return(x);
}

/* function taken from "dvd2sub.c" in the svcdsubs packages in the
   vcdimager contribs directory.  Author unknown, but released under GPL2.
*/


void set_clut(int CLUT_id,int CLUT_entry_id,int Y_value, int Cr_value, int Cb_value, int T_value) {
 int Y,Cr,Cb,R,G,B;

 Y=Y_value;
 Cr=Cr_value;
 Cb=Cb_value;
 B = 1.164*(Y - 16)                    + 2.018*(Cb - 128);
 G = 1.164*(Y - 16) - 0.813*(Cr - 128) - 0.391*(Cb - 128);
 R = 1.164*(Y - 16) + 1.596*(Cr - 128);
 if (B<0) B=0; if (B>255) B=255;
 if (G<0) G=0; if (G>255) G=255;
 if (R<0) R=0; if (R>255) R=255;
 // fprintf(stderr,"Setting colour for CLUT_id=%d, CLUT_entry_id=%d\n",CLUT_id,CLUT_entry_id);

 if ((CLUT_id > 15) || (CLUT_entry_id > 15)) {
    fprintf(stderr,"ERROR: CLUT_id=%d, CLUT_entry_id=%d\n",CLUT_id,CLUT_entry_id);
    exit(1);
  }

 colours[(CLUT_id*48)+(CLUT_entry_id*3)+0]=R;
 colours[(CLUT_id*48)+(CLUT_entry_id*3)+1]=G;
 colours[(CLUT_id*48)+(CLUT_entry_id*3)+2]=B;
 if (Y==0) {
   trans[(CLUT_id*16)+CLUT_entry_id]=0;
 } else {
   trans[(CLUT_id*16)+CLUT_entry_id]=255;
 }
}

void decode_4bit_pixel_code_string(int r, int object_id, int ofs, int n) {
  int next_bits,
      switch_1,
      switch_2,
      switch_3,
      run_length,
      pixel_code;

  int bits;
  unsigned int data;
  int j;

  if (in_scanline==0) {
    in_scanline=1;
  }
  nibble_flag=0;
  j=i+n;
  while(i < j) {
//    printf("start of loop, i=%d, nibble-flag=%d\n",i,nibble_flag);
//    printf("buf=%02x %02x %02x %02x\n",buf[i],buf[i+1],buf[i+2],buf[i+3]);

    bits=0;
    pixel_code=0;
    next_bits=next_nibble();

    if (next_bits!=0) {
      pixel_code=next_bits;
      plot(r,1,pixel_code);
      bits+=4;
    } else {
      bits+=4;
      data=next_nibble();
      switch_1=(data&0x08)>>3;
      bits++;
      if (switch_1==0) {
        run_length=(data&0x07);
        bits+=3;
        if (run_length!=0) {
          plot(r,run_length+2,pixel_code);
        } else {
//          printf("end_of_string - run_length=%d\n",run_length);
          break;
        }
      } else {
        switch_2=(data&0x04)>>2;
        bits++;
        if (switch_2==0) {
          run_length=(data&0x03);
          bits+=2;
          pixel_code=next_nibble();
          bits+=4;
          plot(r,run_length+4,pixel_code);
        } else {
          switch_3=(data&0x03);
          bits+=2;
          switch (switch_3) {
            case 0: plot(r,1,pixel_code);
                    break;
            case 1: plot(r,2,pixel_code);
                    break;
            case 2: run_length=next_nibble();
                    bits+=4;
                    pixel_code=next_nibble();
                    bits+=4;
                    plot(r,run_length+9,pixel_code);
                    break;
            case 3: run_length=next_nibble();
                    run_length=(run_length<<4)|next_nibble();
                    bits+=8;
                    pixel_code=next_nibble();
                    bits+=4;
                    plot(r,run_length+25,pixel_code);
          }
        }
      }
    }

  }
  if (nibble_flag==1) {
    i++;
    nibble_flag=0;
  }
}


void process_pixel_data_sub_block(int r, int o, int ofs, int n) {
  int data_type;
  int j;

  j=i+n;

  x=(regions[r].object_pos[o])>>16;
  y=((regions[r].object_pos[o])&0xffff)+ofs;
//  fprintf(stderr,"process_pixel_data_sub_block: r=%d, x=%d, y=%d, o=%d, ofs=%d, n=%d\n",r,x,y,o,ofs);
//  fprintf(stderr,"process_pixel_data: %02x %02x %02x %02x %02x %02x\n",buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5]);
  while (i < j) {
    data_type=buf[i++];

    switch(data_type) {
      case 0: i++;
      case 0x11: decode_4bit_pixel_code_string(r,o,ofs,n-1);
                 break;
      case 0xf0: in_scanline=0;
                 x=(regions[r].object_pos[o])>>16;
                 y+=2;
                 break;
      default: fprintf(stderr,"unimplemented data_type %02x in pixel_data_sub_block\n",data_type);
    }
  }

  i=j;
}
void process_page_composition_segment() {
  int page_id,
      segment_length,
      page_time_out,
      page_version_number,
      page_state;
  int region_id,region_x,region_y;
  int j;
  int r;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;

  j=i+segment_length;

  page_time_out=buf[i++];
  page_version_number=(buf[i]&0xf0)>>4;
  page_state=(buf[i]&0x0c)>>2;
  i++;
//  fprintf(stderr,"PAGE_COMPOSITION_SEGMENT: page_id=%04x, page_time_out=%d, page_version=%d,page_state=%d\n",page_id,page_time_out,page_version_number,page_state);

  //  fprintf(stderr,"page_state=%d\n",page_state);
  if ((acquired==0) && (page_state!=2) && (page_state!=1)) {
    //    fprintf(stderr,"waiting for mode_change\n");
    return;
  } else {
    acquired=1;
  }

  for (r=0;r<MAX_REGIONS;r++) {
    page.regions[r].is_visible=0;
  }
  while (i<j) {
    region_id=buf[i++];
    i++; // reserved
    region_x=(buf[i]<<8)|buf[i+1]; i+=2;
    region_y=(buf[i]<<8)|buf[i+1]; i+=2;

    page.regions[region_id].x=region_x;
    page.regions[region_id].y=region_y;
    page.regions[region_id].is_visible=1;
 
  }  
}

void process_region_composition_segment() {
  int page_id,
      segment_length,
      region_id,
      region_version_number,
      region_fill_flag,
      region_width,
      region_height,
      region_level_of_compatibility,
      region_depth,
      CLUT_id,
      region_8_bit_pixel_code,
      region_4_bit_pixel_code,
      region_2_bit_pixel_code;
  int object_id,
      object_type,
      object_provider_flag,
      object_x,
      object_y,
      foreground_pixel_code,
      background_pixel_code;
  int j;
  int o;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;

  j=i+segment_length;

  region_id=buf[i++];
  region_version_number=(buf[i]&0xf0)>>4;
  region_fill_flag=(buf[i]&0x08)>>3;
  i++;
  region_width=(buf[i]<<8)|buf[i+1]; i+=2;
  region_height=(buf[i]<<8)|buf[i+1]; i+=2;
  region_level_of_compatibility=(buf[i]&0xe0)>>5;
  region_depth=(buf[i]&0x1c)>>2;
  i++;
  CLUT_id=buf[i++];
  region_8_bit_pixel_code=buf[i++];
  region_4_bit_pixel_code=(buf[i]&0xf0)>>4;
  region_2_bit_pixel_code=(buf[i]&0x0c)>>2;
  i++;


  if (regions[region_id].win < 0) {
    // If the region doesn't exist, then open it.
    create_region(region_id,region_width,region_height,region_depth);
    regions[region_id].CLUT_id=CLUT_id;
  }

  regions[region_id].width=region_width;
  regions[region_id].height=region_height;

  if (region_fill_flag==1) {
    //    fprintf(stderr,"filling region %d with %d\n",region_id,region_4_bit_pixel_code);
    memset(regions[region_id].img,region_4_bit_pixel_code,sizeof(regions[region_id].img));
  }

  regions[region_id].objects_start=i;  
  regions[region_id].objects_end=j;  

  for (o=0;o<65536;o++) {
    regions[region_id].object_pos[o]=0xffffffff;
  }

  while (i < j) {
    object_id=(buf[i]<<8)|buf[i+1]; i+=2;
    object_type=(buf[i]&0xc0)>>6;
    object_provider_flag=(buf[i]&0x30)>>4;
    object_x=((buf[i]&0x0f)<<8)|buf[i+1]; i+=2;
    object_y=((buf[i]&0x0f)<<8)|buf[i+1]; i+=2;

    regions[region_id].object_pos[object_id]=(object_x<<16)|object_y;
      
    if ((object_type==0x01) || (object_type==0x02)) {
      foreground_pixel_code=buf[i++];
      background_pixel_code=buf[i++];
    }
  }
}

void process_CLUT_definition_segment() {
  int page_id,
      segment_length,
      CLUT_id,
      CLUT_version_number;

  int CLUT_entry_id,
      CLUT_flag_8_bit,
      CLUT_flag_4_bit,
      CLUT_flag_2_bit,
      full_range_flag,
      Y_value,
      Cr_value,
      Cb_value,
      T_value;

  int j;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;
  j=i+segment_length;

  CLUT_id=buf[i++];
  CLUT_version_number=(buf[i]&0xf0)>>4;
  i++;

  while (i < j) {
    CLUT_entry_id=buf[i++];
      
    CLUT_flag_2_bit=(buf[i]&0x80)>>7;
    CLUT_flag_4_bit=(buf[i]&0x40)>>6;
    CLUT_flag_8_bit=(buf[i]&0x20)>>5;
    full_range_flag=buf[i]&1;
    i++;
    if (full_range_flag==1) {
      Y_value=buf[i++];
      Cr_value=buf[i++];
      Cb_value=buf[i++];
      T_value=buf[i++];
    } else {
      Y_value=(buf[i]&0xfc)>>2;
      Cr_value=(buf[i]&0x2<<2)|((buf[i+1]&0xc0)>>6);
      Cb_value=(buf[i+1]&0x2c)>>2;
      T_value=buf[i+1]&2;
      i+=2;
    }
    set_clut(CLUT_id,CLUT_entry_id,Y_value,Cr_value,Cb_value,T_value);
  }
}

void process_object_data_segment() {
  int page_id,
      segment_length,
      object_id,
      object_version_number,
      object_coding_method,
      non_modifying_colour_flag;
      
  int top_field_data_block_length,
      bottom_field_data_block_length;
      
  int j;
  int old_i;
  int r;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;
  j=i+segment_length;
  
  object_id=(buf[i]<<8)|buf[i+1]; i+=2;
  curr_obj=object_id;
  object_version_number=(buf[i]&0xf0)>>4;
  object_coding_method=(buf[i]&0x0c)>>2;
  non_modifying_colour_flag=(buf[i]&0x02)>>1;
  i++;

  //  fprintf(stderr,"decoding object %d\n",object_id);
  old_i=i;
  for (r=0;r<MAX_REGIONS;r++) {
    // If this object is in this region...
   if (regions[r].win >= 0) {
     //    fprintf(stderr,"testing region %d, object_pos=%08x\n",r,regions[r].object_pos[object_id]);
    if (regions[r].object_pos[object_id]!=0xffffffff) {
      //      fprintf(stderr,"rendering object %d into region %d\n",object_id,r);
      i=old_i;
      if (object_coding_method==0) {
        top_field_data_block_length=(buf[i]<<8)|buf[i+1]; i+=2;
        bottom_field_data_block_length=(buf[i]<<8)|buf[i+1]; i+=2;

        process_pixel_data_sub_block(r,object_id,0,top_field_data_block_length);

        process_pixel_data_sub_block(r,object_id,1,bottom_field_data_block_length);
      }
    }
   }
  }
}

void save_png(unsigned char* filename) {
  int r;
  int x,y,out_y;
  bitmap_t bitmap;
  int v;
  int found=0;
  int count;

  out_y=0;
  init_bitmap(&bitmap,720,576,0);

  count=0;
  for (r=0;r<MAX_REGIONS;r++) {
    if (regions[r].win >= 0) {
      if (page.regions[r].is_visible) {
        count++;
        fprintf(stderr,"PNG: displaying region %d at %d,%d width=%d,height=%d\n",r,page.regions[r].x,page.regions[r].y,regions[r].width,regions[r].height);
	out_y=page.regions[r].y*720;
        for (y=0;y<regions[r].height;y++) {
          for (x=0;x<regions[r].width;x++) {
            v=regions[r].img[(y*regions[r].width)+x];
            if (v>0) { found=1; }
            bitmap.buffer[out_y+x+page.regions[r].x]=v+16*regions[r].CLUT_id;
          }
          out_y+=720;
        }
      }
    }
  }
  write_png(&bitmap,filename,colours,trans,256);
}

int main(int argc, char* argv[]) {
  int n;
  int fd;
  int pid;
  int is_num;
  int vdrmode=0;
  char filename[20];
  int fileno=1;
  int page_id;
  int new_i;

  int PES_packet_length;
  int PES_header_data_length;

  uint64_t PTS;
  int r; 
  int data_identifier,subtitle_stream_id;

  int segment_length,
      segment_type;
  
  fprintf(stderr,"dvbsubs v%s - (C) Dave Chapman 2002-2004\n",VERSION);
  fprintf(stderr,"Latest version available from http://www.linuxstb.org\n\n");

  if (argc!=2) {
    fprintf(stderr,"USAGE: dvbsubs PID < filename.ts\n");
    fprintf(stderr,"    or dvbsubs -vdr < filename.vdr\n");
    exit(0);
  }

  is_num=1;
  for (n=0;n<strlen(argv[1]);n++) {
    if (!(isdigit(argv[1][n]))) is_num=0;
  }

  fd=0;
  if (is_num) {
    pid=atoi(argv[1]);
  } else {
    if (!strcmp(argv[1],"-vdr")) {
      vdrmode=1;
    } else {
      fprintf(stderr,"Command-line error.\n");
      exit(1);
    }
  }

  init_data();

  gettimeofday(&start_tv,NULL);

  PES_packet_length=read_pes_packet(fd,pid,buf,vdrmode);
//  fprintf(stderr,"READ PES PACKET: length=%d\n",PES_packet_length);
  while (PES_packet_length >= 0) {
    PTS=get_pes_pts(buf);
    if (first_PTS==0) { first_PTS=PTS; }

    fprintf(stderr,"%s\r",pts2hmsu(PTS-first_PTS,'.'));


    PES_header_data_length=buf[8];
//    fprintf(stderr,"PES_header_data_length=%d\n",PES_header_data_length);
    i=9+PES_header_data_length;

    data_identifier=buf[i++];
    subtitle_stream_id=buf[i++];

    if (data_identifier!=0x20) {
      fprintf(stderr,"ERROR: PES data_identifier != 0x20 (%02x), aborting\n",data_identifier);
      exit(1);
    }
    if (subtitle_stream_id!=0) {
      fprintf(stderr,"ERROR: subtitle_stream_id != 0 (%02x), aborting\n",subtitle_stream_id);
      exit(1);
    }

    while(i < (PES_packet_length-1)) {
      /* SUBTITLING SEGMENT */
      if (buf[i]!=0x0f) { 
        fprintf(stderr,"\nERROR: sync byte not present, skipping rest of PES packet - next PNG is sub%05d.png\n",fileno);
        i=PES_packet_length;
        continue;
      }
      i++;
      segment_type=buf[i++];
//      fprintf(stderr,"Processing segment_type 0x%02x\n",segment_type);

      page_id=(buf[i]<<8)|buf[i+1]; 
      segment_length=(buf[i+2]<<8)|buf[i+3];

      new_i=i+segment_length+4;

      /* SEGMENT_DATA_FIELD */
      switch(segment_type) {
        case 0x10: process_page_composition_segment(); 
                   break;
        case 0x11: process_region_composition_segment();
                   break;
        case 0x12: process_CLUT_definition_segment();
                   break;
        case 0x13: process_object_data_segment();
                   break;
        case 0x80: // end_of_display_set_segment(); - IMPLEMENTATION IS OPTIONAL - dvbsubs ignores it.
                   break;
        default:
          fprintf(stderr,"ERROR: Unknown segment %02x, length %d, data=%02x %02x %02x %02x\n",segment_type,segment_length,buf[i+4],buf[i+5],buf[i+6],buf[i+7]);
          exit(1);
       }

      i=new_i;
    }   

    if (acquired) {
      acquired=0;
      n=0;
      for (r=0;r<MAX_REGIONS;r++) {
        if (regions[r].win >= 0) {
          if (page.regions[r].is_visible) {
            n++;
          }
        }
      }
      if (n) {
        sprintf(filename,"sub%05d.png",fileno++);
        fprintf(stdout,"<spu start=\"%s\" image=\"%s\" />\n",pts2hmsu(PTS-first_PTS,'.'),filename);
        save_png(filename);
      }
    }
    PES_packet_length=read_pes_packet(fd,pid,buf,vdrmode);
//    fprintf(stderr,"READ PES PACKET: length=%d\n",PES_packet_length);
  }
  fprintf(stderr,"\nEND OF INPUT FILE.\n");
  return(0);
}
