/* 

dvbtextsubs - a teletext subtitles decoder for DVB cards. 
(C) Dave Chapman <dave@dchapman.com> 2003-2004.

The latest version can be found at http://www.linuxstb.org/

Thanks to:  

Ralph Metzler (re: dvbtext - dvbtextsubs is based heavily on dvbtext)
for his work on both the DVB driver and his old vbidecode package
(some code and ideas in dvbtext are borrowed from vbidecode).

Jan Pantelje for his advice and his work on submux-dvd

Ragnar Sundblad (the author of the VDR teletext subtitles plugin) for
his help in adding VDR support to dvbtextsubs and for suggesting
various improvements.

Scott T. Smith for creating "dvdauthor".

Copyright notice:

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

// dvbtext includes:
#include "tables.h"
#include "vtxdecode.h"
#include "pes.h"

#define VERSION "0.3"
#define USAGE "\n\
USAGE: dvbtextsubs pid pageno\n\
or     dvbtextsubs -vdr pageno\n\n\
The DVB stream must be piped to dvbtextsubs.  e.g.:\n\n\
cat 0*.vdr | dvbtextsubs > output.xml\n\n\
Options: -srt      Output subtitles in Subviewer format\n\
         -pts      PTS offset (in ms) to add to every PTS in output file\n"

typedef enum {
   SUBFORMAT_XML,
   SUBFORMAT_SUBVIEWER
} subformat_t;

subformat_t subformat;

int debug=0;
int no_pts_warning=0;

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

int verbose=0;
int displayed_header=0;
uint64_t PTS=0;
uint64_t USER_PTS=0;
uint64_t audio_pts=0;
uint64_t first_audio_pts=0;
uint64_t FIRST_PTS=0;
int sub_count=0;

typedef struct {
  unsigned char lang;
  uint64_t start_PTS;
  uint64_t end_PTS;
  int has_end_pts;
  int num_lines;
  int row_no[25];
  char lines[25][40];
  int first_char[25];
  int last_char[25];
} subtitle_t;

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
//fprintf(stderr,"PES_packet_length=%d\n",PES_packet_length);

  if ((stream_id&0xe0)==0xe0) {
    // Video stream - PES_packet_length must be equal to zero
    if (PES_packet_length!=0) {
      fprintf(stderr,"ERROR IN VIDEO PES STREAM: PES_packet_length=%d (must be zero)\n",PES_packet_length);
      return(0);
    }
  } else {
    if (PES_packet_length+6!=n) {
      fprintf(stderr,"ERROR IN NON-VIDEO PES: n=%d,PES_packet_length=%d\n",n,PES_packet_length);
      return(0);
    }
  }
  return(1);
}

static const char* colours[]= {
  "black","red","lime","yellow","blue","magenta","cyan","white"
};

// There seems to be a limit of 8 teletext streams - OK for most (but
// not all) transponders.
#define MAX_CHANNELS 8

typedef struct mag_struct_ {
   int valid;
   int mag;
   unsigned char flags;
   unsigned char lang;
   uint64_t PTS;
   int line_transmitted[25];
   int num_valid_lines;
   int pnum,sub;
   unsigned char pagebuf[25*40];
} mag_struct;

  mag_struct prevpage;
  mag_struct thepage;

// FROM vbidecode
// unham 2 bytes into 1, report 2 bit errors but ignore them
unsigned char unham(unsigned char a,unsigned char b)
{
  unsigned char c1,c2;
  
  c1=unhamtab[a];
  c2=unhamtab[b];
//  if ((c1|c2)&0x40) 
//      fprintf(stderr,"bad ham!");
  return (c2<<4)|(0x0f&c1);
}

void xml_output_char(FILE* fp, unsigned char ch) {

  switch(ch) {
    case '<': fprintf(fp,"&lt;"); break;
    case '>': fprintf(fp,"&gt;"); break;
    case '&': fprintf(fp,"&amp;"); break;
//  case '"': fprintf(fp,"&quote;"); break;
    default:  fprintf(fp,"%c",ch); break;
  }
}

int is_first=1;
subtitle_t subtitle;
subtitle_t prev_subtitle;

void print_xml(FILE* fd, subtitle_t* subtitle) {
  int i;
  char ch;
  int row;
  char tmp[41];
  int n;
  int colour;

  if (subtitle->num_lines==0) { return; }

//  printf("\"");
//  for (i=1;i<=40;i++) printf("%c",'0'+(i%10));
//  printf("\"\n");

  for (row=0;row<subtitle->num_lines;row++) {
    subtitle->first_char[row]=-1;
    i=0;
    n=0;
    for (i=1;i<=40;i++) {
      ch=subtitle->lines[row][i-1]&0x7f;
      if ((subtitle->first_char[row]<0) && (ch > 32)) subtitle->first_char[row]=i;
      if (ch < 32) ch=32;
      tmp[n++]=ch;
    }
    subtitle->last_char[row]=40;
    while ((subtitle->last_char[row] >0) && (tmp[subtitle->last_char[row]-1]==' ')) subtitle->last_char[row]--;
    tmp[n]=0;
//    fprintf(fd,"\"%s\"\n",tmp);
//    fprintf(fd,"First_char[%d]=%d, last_char[%d]=%d\n",row,subtitle->first_char[row],row,subtitle->last_char[row]);
  }

  fprintf(fd,"    <spu lang=\"%s\" start=\"%s\"",langs[subtitle->lang],pts2hmsu(subtitle->start_PTS));
  fprintf(stderr,"%s\r",pts2hmsu(subtitle->start_PTS));
  if (subtitle->has_end_pts) {
    fprintf(fd," end=\"%s\"",pts2hmsu(subtitle->end_PTS));
  }
  fprintf(fd,">\n");
  
  for (row=0;row<subtitle->num_lines;row++) {
    fprintf(fd,"      <line row=\"%d\" col=\"%d\">",subtitle->row_no[row],subtitle->first_char[row]);
    colour=-1;
    for (i=1;i<=40;i++) {
      ch=subtitle->lines[row][i-1]&0x7f;
      if (ch >= ' ') {
        if ((i >= subtitle->first_char[row]) && (i <= subtitle->last_char[row])) {
          if (colour==-1) {
            colour=7; fprintf(fd,"<%s>",colours[colour]);
          }
          xml_output_char(fd,vtx2iso8559_1_table[subtitle->lang][ch-32]);
        }
      } else {
        if ((i >= subtitle->first_char[row]) && (i <= subtitle->last_char[row])) fprintf(fd," ");
        if (ch < 8) {
          if (colour!=-1) {
            fprintf(fd,"</%s>",colours[colour]);
          }
          colour=ch;
          fprintf(fd,"<%s>",colours[colour]);
        } else if (ch == 29) {
          fprintf(fd,"<bg />");
        } else if ((ch!=0x0b) && (ch!=0x0a) &&   // Start/end box
                   (ch!=0x0d)) {                 // Double height
          fprintf(fd,"<%02x>",ch);          
        }
      }
    }
    if (colour!=-1) fprintf(fd,"</%s>",colours[colour]);
    fprintf(fd,"</line>\n");
  }
  fprintf(fd,"    </spu>\n");
}

void print_subviewer(FILE* fd, subtitle_t* subtitle) {
  int i;
  char ch;
  int row;
  char tmp[41];
  int n;
  int colour;
  int j;

  if (subtitle->num_lines==0) { return; }

//  printf("\"");
//  for (i=1;i<=40;i++) printf("%c",'0'+(i%10));
//  printf("\"\n");

  for (row=0;row<subtitle->num_lines;row++) {
    subtitle->first_char[row]=-1;
    i=0;
    n=0;
    for (i=1;i<=40;i++) {
      ch=subtitle->lines[row][i-1]&0x7f;
      if ((subtitle->first_char[row]<0) && (ch > 32)) subtitle->first_char[row]=i;
      if (ch < 32) ch=32;
      tmp[n++]=ch;
    }
    subtitle->last_char[row]=40;
    while ((subtitle->last_char[row] >0) && (tmp[subtitle->last_char[row]-1]==' ')) subtitle->last_char[row]--;
    tmp[n]=0;
//    fprintf(fd,"\"%s\"\n",tmp);
//    fprintf(fd,"First_char[%d]=%d, last_char[%d]=%d\n",row,subtitle->first_char[row],row,subtitle->last_char[row]);
  }

  fprintf(stderr,"%s\r",pts2hmsu(subtitle->start_PTS));
  fprintf(fd,"%d\n%s --> ",++sub_count,pts2hmsu(subtitle->start_PTS));
  fprintf(fd,"%s\n",pts2hmsu(subtitle->end_PTS));
  
  j=0;
  for (row=0;row<subtitle->num_lines;row++) {
    colour=-1;
    for (i=1;i<=40;i++) {
      ch=subtitle->lines[row][i-1]&0x7f;
      if (ch >= ' ') {
        if ((i >= subtitle->first_char[row]) && (i <= subtitle->last_char[row])) {
          if (colour==-1) {
            colour=7; // fprintf(fd,"<%s>",colours[colour]);
          }
          xml_output_char(fd,vtx2iso8559_1_table[subtitle->lang][ch-32]);
          j=1;
        }
      } else {
        if ((i >= subtitle->first_char[row]) && (i <= subtitle->last_char[row])) fprintf(fd," ");
        if (ch < 8) {
          if (colour!=-1) {
            // fprintf(fd,"</%s>",colours[colour]);
          }
          colour=ch;
          //fprintf(fd,"<%s>",colours[colour]);
        } else if (ch == 29) {
          //fprintf(fd,"<bg />");
        } else if ((ch!=0x0b) && (ch!=0x0a) &&   // Start/end box
                   (ch!=0x0d)) {                 // Double height
          //fprintf(fd,"<%02x>",ch);          
        }
      }
    }
    //    if (colour!=-1) fprintf(fd,"</%s>",colours[colour]);
    fprintf(fd,"\n");
  }
  fprintf(fd,"\n");
}

void print_page(mag_struct *mag) {
  int i;

  subtitle.num_lines=0;
  subtitle.start_PTS=mag->PTS-FIRST_PTS;
  subtitle.lang=mag->lang;

  if (mag->num_valid_lines>0) {
    for (i=1;i<=23;i++) {
      if (mag->line_transmitted[i]) {
        subtitle.row_no[subtitle.num_lines]=i;
        memcpy(subtitle.lines[subtitle.num_lines],&mag->pagebuf[40*i],40);
        subtitle.num_lines++;
      }
    }
  }
  if (!is_first) {
    if (subtitle.num_lines==0) {
      prev_subtitle.has_end_pts=1;
    } else {
      prev_subtitle.has_end_pts=0;
    }
    prev_subtitle.end_PTS=subtitle.start_PTS; // Store the end time anyway - some formats need it.
    switch (subformat) {
      case SUBFORMAT_XML: print_xml(stdout,&prev_subtitle); break;
      case SUBFORMAT_SUBVIEWER: print_subviewer(stdout,&prev_subtitle); break;
      default: break;
    }
    prev_subtitle=subtitle;
  }
  is_first=0;
}

void debug_print_page(mag_struct *mag) {
  int i,j,flag;
  unsigned char ch;
  int pageflags;
  pageflags=unham(mag->pagebuf[2],mag->pagebuf[3])&0x80;

  fprintf(stdout,"<page pageno=\"%03x\" flags=\"%02x\" charset=\"%s\" pts=\"%lld\" time=\"%.3f\">\n",mag->pnum,pageflags,langs[mag->lang],mag->PTS,(mag->PTS-FIRST_PTS)/1000.0);
  for (i=0;i<=23;i++) {
    if (mag->line_transmitted[i]) {
      flag=0;
      printf("%02d: ",i);
      for (j=0;j<39;j++) printf("%02x ",mag->pagebuf[40*i+j]);
      for (j=0;j<39;j++) {
        ch=(mag->pagebuf[40*i+j]&0x7f);
        if ((ch >=32) && (ch<=127)) { printf("%c",ch); } else { printf("."); }
      }
      printf("\n");
    }
  }

  fprintf(stdout,"</page>\n");  
}

void set_line(int line, unsigned char* data,int mag, int the_page) {
  unsigned char c;
  int i;

  //  fprintf(stderr,"the_page=%04x\n",the_page);
  //  fprintf(stdout,"In set_line - mag=%d, line=%d!\n",mag,line);
  //  if (line==0) { fprintf(stderr,"mag=%d, page=%02x\n",mag,unham(data[0],data[1])); }
  if (mag!=((the_page&0x0f00)>>8)) {
     return;
  }
//    for (i=0;i<44;i++) {
//      if (((data[i]&0x7f) >= 32) && ((data[i]&0x7f) <128)) { fprintf(stdout,"%c",data[i]&0x7f); }
//    }
//  fprintf(stdout,"\n");
  if ((line==0)) { 
    if (thepage.valid) {
      if (thepage.pnum==the_page) {
        if (debug) { 
           fprintf(stdout,"Previous page:\n");
           debug_print_page(&prevpage);
           fprintf(stdout,"Current page:\n");
           debug_print_page(&thepage);
           fprintf(stdout,"Current page subtitle output:\n");
           print_page(&thepage);
           fprintf(stdout,"Actual subtitle output:\n");
        }
        i=bcmp(&thepage.pagebuf[40],&prevpage.pagebuf[40],24*40);
        if (debug) { fprintf(stdout,"Comparison of pages: %d\n",i); }
	if (i!=0) {
          print_page(&thepage);
          prevpage=thepage;
	}
      }
    }
    thepage.pnum=(mag<<8)|unham(data[0],data[1]); // The lower two (hex) numbers of page
    if (thepage.pnum==the_page) {
      thepage.valid=1;
    } else {
      thepage.valid=0;
    }
//    fprintf(stderr,"thepage.pnum=%04x\n",thepage.pnum);
//    if (thepage.pnum==0xff) return;  // These are filler lines. Can use to update clock
    thepage.flags=unham(data[2],data[3])&0x80;
    thepage.flags|=(c&0x40)|((c>>2)&0x20);
    c=unham(data[6],data[7]);
    thepage.flags|=((c<<4)&0x10)|((c<<2)&0x08)|(c&0x04)|((c>>1)&0x02)|((c>>4)&0x01);
    thepage.lang=((c>>5) & 0x07);
    thepage.PTS=PTS;

    if (thepage.flags&0x80) {
      thepage.num_valid_lines=0;
      for (i=1;i<25;i++) { thepage.line_transmitted[i]=0; }
      memset(thepage.pagebuf,' ', 25*40);
    }

    c=unham(data[8],data[9]);

    thepage.sub=(unham(data[4],data[5])<<8)|(unham(data[2],data[3])&0x3f7f);
  } 

  if (thepage.valid) {
    if (line <= 23) {
      thepage.line_transmitted[line]=1;
      memcpy(&thepage.pagebuf[40*line],data,40);
    }
    if (line > 0) { thepage.num_valid_lines++; }
  }
}

int process_pes_packet (unsigned char* buf,int n, int the_page) {
  unsigned char mpag,mag,line;
  int j,k;
  int stream_id;
  int PES_packet_length;
  int PTS_DTS_flags;
  int data_len,data_unit_id;
  uint64_t p0,p1,p2,p3,p4;

  stream_id=buf[3];
  PES_packet_length=(buf[4]<<8)|buf[5];
  //  fprintf(stderr,"n=%d\n",n);
  //  fprintf(stderr,"stream_id=0x%02x\n",stream_id);
  //  fprintf(stderr,"PES_packet_length=%d\n",PES_packet_length);
//  fprintf(stderr,"buf[6]=%02x\n",buf[6]);

  if ((stream_id==0xbd) && ((buf[buf[8]+9]>=0x10) && (buf[buf[8]+9]<=0x1f))) {
    PTS_DTS_flags=(buf[7]&0xb0)>>6;
    if (PTS_DTS_flags==0x02) {
      // PTS is in bytes 9,10,11,12,13
      p0=(buf[13]&0xfe)>>1|((buf[12]&1)<<7);
      p1=(buf[12]&0xfe)>>1|((buf[11]&2)<<6);
      p2=(buf[11]&0xfc)>>2|((buf[10]&3)<<6);
      p3=(buf[10]&0xfc)>>2|((buf[9]&6)<<5);
      p4=(buf[9]&0x08)>>3;
 
      PTS=p0|(p1<<8)|(p2<<16)|(p3<<24)|(p4<<32);
      PTS=PTS/90;
      if (FIRST_PTS==0) { FIRST_PTS=PTS-USER_PTS; }
    } else {
      //fprintf(stdout,"stream_id=%02x, No PTS\n",stream_id);
      if (no_pts_warning==0) {
        fprintf(stderr,"WARNING: No PTS value in teletext packet - using audio PTS.\n");
        fprintf(stderr,"First audio PTS=%lld ms\n",first_audio_pts);
        no_pts_warning=1;
      }
      PTS=audio_pts;
      if (FIRST_PTS==0) { FIRST_PTS=first_audio_pts-USER_PTS; }
    }
    k=buf[8]+9;
//    fprintf(stderr,"PES Header Data Length=%d\n",buf[8]);
//    fprintf(stderr,"HERE: k=%d, n=%d\n",k,n);
    k++;
    while (k < n) {
      data_unit_id=buf[k++];
      data_len=buf[k++];
      if (data_len!=0x2c) { data_len=0x2c; }
//      fprintf(stdout,"data_unit_id=%02x,data_len=%d\n",data_unit_id,data_len);
//      if ((data_unit_id==0x02) || (data_unit_id==0x03)) {
        for (j=k;j<k+data_len;j++) { 
          buf[j]=invtab[buf[j]];
        }
        mpag=unham(buf[k+2],buf[k+3]);
        mag=mpag&7;
        line=(mpag>>3)&0x1f;
        // mag==0 means page is 8nn
        if (mag==0) mag=8;
        set_line(line,&buf[k+4],mag,the_page);
//        }
      k+=data_len;
    }
  } else {
     fprintf(stderr,"This is not a private data type 1 stream - are you sure you specified the correct PID? stream_id=%02x\n",stream_id);
     exit(1);
  }
  return(0);
}


int main(int argc, char** argv) {
  unsigned char pesbuf[1265536];
  uint64_t tmp_pts;
  int pes_format=0;
  int PES_packet_length;
  int i,m;
  int pids[MAX_CHANNELS];
  mag_struct mags[MAX_CHANNELS][8];
  int count;
  int the_page;
  int the_pid;
  is_first=0;
 
  memset(prevpage.pagebuf,' ', 25*40);
  for (i=1;i<25;i++) { prevpage.line_transmitted[i]=0; }
  prevpage.num_valid_lines=0;

  fprintf(stderr,"dvbtextsubs v%s - (C) Dave Chapman 2003-2004\n",VERSION);
  fprintf(stderr,"Latest version available from http://www.linuxstb.org\n");

  //  params.iFrequency=;
  //  params.SymbolRate=;
  //  params.FEC_inner=;

  subformat=SUBFORMAT_XML;

  if (argc==1) {
    fprintf(stderr,USAGE);
    return(-1);
  } else {
    count=0;
    for (i=1;i<argc;i++) {
      if (strcmp(argv[i],"-d")==0) {
        debug=1;
      } else if (strcmp(argv[i],"-vdr")==0) {
        pes_format=1;
      } else if (strcmp(argv[i],"-srt")==0) {
        subformat=SUBFORMAT_SUBVIEWER;
      } else if (strcmp(argv[i],"-pts")==0) {
        i++;
        USER_PTS=atoi(argv[i]);
        fprintf(stderr,"Adding user PTS offset of %lld to every timestamp.\n",USER_PTS);
      } else {
        pids[count]=atoi(argv[i]);
        if (pids[count]) { count++ ; }
      }
    }
  }

  if ((pes_format==1) && (count==1)) {
    the_page=(pids[0]%10)|((((pids[0]-(100*(pids[0]/100)))%100)/10)<<4)|((pids[0]/100)<<8);
  } else if ((pes_format==0) && (count==2)) {
    the_pid=pids[0];
    the_page=(pids[1]%10)|((((pids[1]-(100*(pids[1]/100)))%100)/10)<<4)|((pids[1]/100)<<8);
  } else {
    fprintf(stderr,USAGE);
    return(-1);
  }

  m=0;
  mags[m][0].mag=8;
  for (i=1;i<8;i++) { 
    mags[m][i].mag=i; 
    mags[m][i].valid=0;
  }

  thepage.valid=0;

  if (subformat==SUBFORMAT_XML) {
    printf("<?xml version=\"1.0\" encoding=\"iso-8859-1\" ?>\n");
    printf("<subpictures>\n");
    printf("  <stream forced=\"no\">\n");
  }

  while ((PES_packet_length=read_pes_packet(0,the_pid,pesbuf,pes_format)) > 0) {
    tmp_pts=get_pes_pts(pesbuf);

    process_pes_packet(pesbuf,PES_packet_length+6,the_page);
  }
  if (subformat==SUBFORMAT_XML) {
    printf("  </stream>\n");
    printf("</subpictures>\n");
  }
  exit(0);
}
