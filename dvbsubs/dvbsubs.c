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
#include <fcntl.h>

unsigned char buf[100000];
int i=0;

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

  page_time_out=buf[i++];
  page_version_number=(buf[i]&0xf0)>>4;
  page_state=(buf[i]&0x0c)>>2;
  i++;

  j=i+segment_length-2;
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
  
  j=i+segment_length-10;
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
  object_id=(buf[i]<<8)|buf[i+1]; i+=2;
  object_version_number=(buf[i]&0xf0)>>4;
  object_coding_method=(buf[i]&0x0c)>>2;
  non_modifying_colour_flag=(buf[i]&0x02)>>1;
  i++;

  j=i+segment_length-3;
  
  printf("<object_data_segment page_id=\"0x%02x\" object_id=\"0x%02x\">\n",page_id,object_id);

  printf("<object_version_number>%d</object_version_number>\n",object_version_number);
  printf("<object_coding_method>%d</object_coding_method>\n",object_coding_method);
  printf("<non_modifying_colour_flag>%d</non_modifying_colour_flag>\n",non_modifying_colour_flag);

  while (i < j) {
    i++;
  }

  printf("</object_data_segment>\n");
}


int main(int argc, char* argv[]) {
  int n;
  int fd;
  int x;

  int stream_id,
      PES_packet_length;

  unsigned char PTS_1;
  unsigned short PTS_2,PTS_3;

  int segment_length,
      segment_type;
  
  if (argc!=2) {
    fprintf(stderr,"USAGE: dvbsubs file.pes\n");
    exit(0);
  }
  
  fd=open(argv[1],O_RDONLY);
  if (fd <= 0) {
    fprintf(stderr,"can't open file\n");
    exit(0);
  }

  while (1) {
    /* READ PES PACKET */
    n=read(fd,buf,6);

    if ((buf[0]!=0) || (buf[1]!=0) || (buf[2]!=1)) {
      fprintf(stdout,"CAN NOT FIND PES PACKET\n");
      exit(-1);
    }
    i=3;
    stream_id=buf[i++];
    PES_packet_length=(buf[i]<<8)|buf[i+1]; i+=2;
    n=read(fd,&buf[6],PES_packet_length);

    printf("PES_packet_length=%d\n",PES_packet_length);
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
    PTS_1=(buf[i++]&0x0e)<<28;  // 3 bits
    PTS_2=(buf[i]<<7)|((buf[i+1]&0xfe)>>1);         // 15 bits
    i+=2;
    PTS_3=(buf[i]<<7)|((buf[i+1]&0xfe)>>1);         // 15 bits
    i+=2;

    printf("i=%d\n",i);
    printf("<pes_packet data_identifier=\"0x%02x\">\n",buf[i++]);
    printf("<pts pts1=\"0x%01x\"  pts1=\"0x%04x\"  pts1=\"0x%04x\" />\n",PTS_1,PTS_2,PTS_3);
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
//      case 0x12: process_CLUT_definition_segment();
//                 break;
        case 0x13: process_object_data_segment();
                   break;
        default:
          segment_length=(buf[i+2]<<8)|buf[i+3];
          i+=segment_length+4;
          printf("SKIPPING segment %02x, length %d\n",segment_type,segment_length);
      }
    }   
    printf("</subtitle_stream>\n");
    printf("</pes_packet>\n");
  }
}
