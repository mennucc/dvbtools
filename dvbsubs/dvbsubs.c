/*
   dvbsubs - a program for decoding DVB subtitles (ETS 300 743)

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


#include <stdio.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <sys/poll.h>

#include <ost/dmx.h>

unsigned char buf[100000];
int i=0;
int nibble_flag=0;
int in_scanline=0;

ssize_t safe_read(int fd, void *buf, size_t count) {
 ssize_t n,tot;

 tot=0;
 while (tot < count) {
   n=read(fd,buf,count-tot);
   tot+=n;
 }
 return(tot);
}

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

void decode_4bit_pixel_code_string(int n) {
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
    printf("<scanline>\n");
    in_scanline=1;
  }
  nibble_flag=0;
  j=i+n;
  while(i < j) {
//    printf("start of loop, i=%d, nibble-flag=%d\n",i,nibble_flag);
//    printf("buf=%02x %02x %02x %02x\n",buf[i],buf[i+1],buf[i+2],buf[i+3]);

    bits=0;
    next_bits=next_nibble();

    if (next_bits!=0) {
      pixel_code=next_bits;
      printf("<pixel run_length=\"1\" pixel_code=\"%d\" />\n",pixel_code);
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
          printf("<pixel run_length=\"%d\" pixel_code=\"0\" />\n",run_length+2);
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
          printf("<pixel run_length=\"%d\" pixel_code=\"%d\" />\n",run_length+4,pixel_code);
        } else {
          switch_3=(data&0x03);
          bits+=2;
          switch (switch_3) {
            case 0: printf("<pixel run_length=\"1\" pixel_code=\"0\" />\n");
                    break;
            case 1: printf("<pixel run_length=\"2\" pixel_code=\"0\" />\n");
                    break;
            case 2: run_length=next_nibble();
                    bits+=4;
                    pixel_code=next_nibble();
                    bits+=4;
                    printf("<pixel run_length=\"%d\", pixel_code=\"%d\" />\n",run_length+9,pixel_code);
                    break;
            case 3: run_length=next_nibble();
                    run_length=(run_length<<4)|next_nibble();
                    bits+=8;
                    pixel_code=next_nibble();
                    bits+=4;
                    printf("<pixel run_length=\"%d\" pixel_code=\"%d\" />\n",run_length+25,pixel_code);
          }
        }
      }
    }

//    printf("used %d bits\n",bits);
  }
  if (nibble_flag==1) {
    i++;
    nibble_flag=0;
  }
}


void process_pixel_data_sub_block(int n) {
  int data_type;
  int j;

  j=i+n;

//  printf("process_pixel_data: %02x %02x %02x %02x %02x %02x\n",buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5]);
  while (i < j) {
    data_type=buf[i++];

//    printf("<data_type>%02x</data_type>\n",data_type);

    switch(data_type) {
      case 0x11: decode_4bit_pixel_code_string(n-1);
                 break;
      case 0xf0: printf("</scanline>\n");
                 in_scanline=0;
                 break;
      default: fprintf(stderr,"unimplemented data_type %02x in pixel_data_sub_block\n",data_type);
    }
  }

  i=j;
}
void process_page_composition_segment() {
  int segment_type,
      page_id,
      segment_length,
      page_time_out,
      page_version_number,
      page_state;
  int region_id,region_x,region_y;
  int j;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;

  j=i+segment_length;

  page_time_out=buf[i++];
  page_version_number=(buf[i]&0xf0)>>4;
  page_state=(buf[i]&0x0c)>>2;
  i++;

  printf("<page_composition_segment page_id=\"0x%02x\">\n",page_id);
  printf("<page_time_out>%d</page_time_out>\n",page_time_out);
  printf("<page_version_number>%d</page_version_number>\n",page_version_number);
  printf("<page_state>");
  switch(page_state) {
     case 0: printf("normal_case"); break ;
     case 1: printf("acquisition_point"); break ;
     case 2: printf("mode_change"); break ;
     case 3: printf("reserved"); break ;
  }
  printf("</page_state>\n");
  
  printf("<page_regions>\n");
  while (i<j) {
    region_id=buf[i++];
    i++; // reserved
    region_x=(buf[i]<<8)|buf[i+1]; i+=2;
    region_y=(buf[i]<<8)|buf[i+1]; i+=2;

    printf("<page_region id=\"%02x\" x=\"%d\" y=\"%d\" />\n",region_id,region_x,region_y);
  }  
  printf("</page_regions>\n");
  printf("</page_composition_segment>\n");
}

void process_region_composition_segment() {
  int segment_type,
      page_id,
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

  printf("<region_composition_segment page_id=\"0x%02x\" region_id=\"0x%02x\">\n",page_id,region_id);

  printf("<region_version_number>%d</region_version_number>\n",region_version_number);
  printf("<region_fill_flag>%d</region_fill_flag>\n",region_fill_flag);
  printf("<region_width>%d</region_width>\n",region_width);
  printf("<region_height>%d</region_height>\n",region_height);
  printf("<region_level_of_compatibility>%d</region_level_of_compatibility>\n",region_level_of_compatibility);
  printf("<region_depth>%d</region_depth>\n",region_depth);
  printf("<CLUT_id>%d</CLUT_id>\n",CLUT_id);
  printf("<region_8_bit_pixel_code>%d</region_8_bit_pixel_code>\n",region_8_bit_pixel_code);
  printf("<region_4_bit_pixel_code>%d</region_4_bit_pixel_code>\n",region_4_bit_pixel_code);
  printf("<region_2_bit_pixel_code>%d</region_2_bit_pixel_code>\n",region_2_bit_pixel_code);
  
  printf("<objects>\n");
  while (i < j) {
    object_id=(buf[i]<<8)|buf[i+1]; i+=2;
    object_type=(buf[i]&0xc0)>>6;
    object_provider_flag=(buf[i]&0x30)>>4;
    object_x=((buf[i]&0x0f)<<8)|buf[i+1]; i+=2;
    object_y=((buf[i]&0x0f)<<8)|buf[i+1]; i+=2;
      
    printf("<object id=\"0x%02x\" type=\"0x%02x\">\n",object_id,object_type);
    printf("<object_provider_flag>%d</object_provider_flag>\n",object_provider_flag);
    printf("<object_x>%d</object_x>\n",object_x);
    printf("<object_y>%d</object_y>\n",object_y);
    if ((object_type==0x01) || (object_type==0x02)) {
      foreground_pixel_code=buf[i++];
      background_pixel_code=buf[i++];
      printf("<foreground_pixel_code>%d</foreground_pixel_code>\n",foreground_pixel_code);
      printf("<background_pixel_code>%d</background_pixel_code>\n",background_pixel_code);
    }

    printf("</object>\n");
  }
  printf("</objects>\n");
  printf("</region_composition_segment>\n");
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

  printf("<CLUT_definition_segment page_id=\"0x%02x\" CLUT_id=\"0x%02x\">\n",page_id,CLUT_id);

  printf("<CLUT_version_number>%d</CLUT_version_number>\n",CLUT_version_number);
  printf("<CLUT_entries>\n");
  while (i < j) {
    CLUT_entry_id=buf[i++];
      
    printf("<CLUT_entry id=\"0x%02x\">\n",CLUT_entry_id);
    CLUT_flag_2_bit=(buf[i]&0x80)>>7;
    CLUT_flag_4_bit=(buf[i]&0x40)>>6;
    CLUT_flag_8_bit=(buf[i]&0x20)>>5;
    full_range_flag=buf[i]&1;
    i++;
    printf("<CLUT_flag_2_bit>%d</CLUT_flag_2_bit>\n",CLUT_flag_2_bit);
    printf("<CLUT_flag_4_bit>%d</CLUT_flag_4_bit>\n",CLUT_flag_4_bit);
    printf("<CLUT_flag_8_bit>%d</CLUT_flag_8_bit>\n",CLUT_flag_8_bit);
    printf("<full_range_flag>%d</full_range_flag>\n",full_range_flag);
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
    printf("<Y_value>%d</Y_value>\n",Y_value);
    printf("<Cr_value>%d</Cr_value>\n",Cr_value);
    printf("<Cb_value>%d</Cb_value>\n",Cb_value);
    printf("<T_value>%d</T_value>\n",T_value);
    printf("</CLUT_entry>\n");
  }
  printf("</CLUT_entries>\n");
  printf("</CLUT_definition_segment>\n");
}

void process_object_data_segment() {
  int segment_type,
      page_id,
      segment_length,
      object_id,
      object_version_number,
      object_coding_method,
      non_modifying_colour_flag;
      
  int top_field_data_block_length,
      bottom_field_data_block_length;
      
  int j;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;
  j=i+segment_length;
  
  object_id=(buf[i]<<8)|buf[i+1]; i+=2;
  object_version_number=(buf[i]&0xf0)>>4;
  object_coding_method=(buf[i]&0x0c)>>2;
  non_modifying_colour_flag=(buf[i]&0x02)>>1;
  i++;

  printf("<object_data_segment page_id=\"0x%02x\" object_id=\"0x%02x\">\n",page_id,object_id);

  printf("<object_version_number>%d</object_version_number>\n",object_version_number);
  printf("<object_coding_method>%d</object_coding_method>\n",object_coding_method);
  printf("<non_modifying_colour_flag>%d</non_modifying_colour_flag>\n",non_modifying_colour_flag);

  if (object_coding_method==0) {
    top_field_data_block_length=(buf[i]<<8)|buf[i+1]; i+=2;
    bottom_field_data_block_length=(buf[i]<<8)|buf[i+1]; i+=2;

    printf("<pixel_data_sub_block type=\"top\" length=\"0x%04x\"/>\n",top_field_data_block_length);

    process_pixel_data_sub_block(top_field_data_block_length);

    printf("<pixel_data_sub_block type=\"bottom\" length=\"0x%04x\"/>\n",bottom_field_data_block_length);
    process_pixel_data_sub_block(bottom_field_data_block_length);
  }
  printf("</object_data_segment>\n");
}


int main(int argc, char* argv[]) {
  int n;
  int fd;
  int x;
  int pid;
  int is_num;

  int stream_id,
      PES_packet_length;

  unsigned long long PTS;
  unsigned char PTS_1;
  unsigned short PTS_2,PTS_3;
  double PTS_secs;

  int segment_length,
      segment_type;
  
  if (argc!=2) {
    fprintf(stderr,"USAGE: dvbsubs filename.pes\n");
    fprintf(stderr,"    or dvbsubs PID\n");
    exit(0);
  }

  is_num=1;
  for (n=0;n<strlen(argv[1]);n++) {
    if (!(isdigit(argv[1][n]))) is_num=0;
  }
      
  if (is_num) {
    pid=atoi(argv[1]);
    if((fd = open("/dev/ost/demux",O_RDWR)) < 0){
      perror("DEMUX DEVICE 1: ");
      return -1;
    }
    set_filt(fd,pid,DMX_PES_OTHER); 
  } else {
    fd=open(argv[1],O_RDONLY);
    if (fd <= 0) {
      fprintf(stderr,"can't open file\n");
      exit(0);
    }
  }

  printf("<?xml version=\"1.0\" ?>\n");
  while (1) {
    /* READ PES PACKET */
    n=safe_read(fd,buf,3);

    while((buf[0]!=0) || (buf[1]!=0) || (buf[2]!=1)) {
      buf[0]=buf[1];
      buf[1]=buf[2];
      n=safe_read(fd,&buf[2],1);
    }
    i=3;

    n=safe_read(fd,&buf[3],3);
    stream_id=buf[i++];
    PES_packet_length=(buf[i]<<8)|buf[i+1]; i+=2;
    n=safe_read(fd,&buf[6],PES_packet_length);
    if (n!=PES_packet_length) { exit(-1); }

    i++;  // Skip some boring PES flags
    if (buf[i]!=0x80) {
     fprintf(stdout,"UNEXPECTED PES HEADER: %02x\n",buf[i]);
     exit(-1);
    }
    i++; 
    if (buf[i]!=5) {
     fprintf(stdout,"UNEXPECTED PES HEADER DATA LENGTH: %d\n",buf[i]);
     exit(-1);
    }
    i++;  // Header data length
    PTS_1=(buf[i++]&0x0e)>>1;  // 3 bits
    PTS_2=(buf[i]<<7)|((buf[i+1]&0xfe)>>1);         // 15 bits
    i+=2;
    PTS_3=(buf[i]<<7)|((buf[i+1]&0xfe)>>1);         // 15 bits
    i+=2;

    PTS=PTS_1;
    PTS=(PTS << 15)|PTS_2;
    PTS=(PTS << 15)|PTS_3;

    PTS_secs=(PTS/90000.0);

    printf("<pes_packet data_identifier=\"0x%02x\" pts_secs=\"%.02f\">\n",buf[i++],PTS_secs);
    printf("<subtitle_stream id=\"0x%02x\">\n",buf[i++]);
    while (buf[i]==0x0f) {
      /* SUBTITLING SEGMENT */
      i++;  // sync_byte
      segment_type=buf[i++];

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
        default:
          segment_length=(buf[i+2]<<8)|buf[i+3];
          i+=segment_length+4;
//          printf("SKIPPING segment %02x, length %d\n",segment_type,segment_length);
      }
    }   
    printf("</subtitle_stream>\n");
    printf("</pes_packet>\n");
  }
}
