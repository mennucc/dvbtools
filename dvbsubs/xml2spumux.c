/*

   xml2submux - a program for generating DVD subtitle bitmaps from an 
                XML file.

   Copyright (C) Dave Chapman 2003
  
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

#include <libxml/parser.h>
#include <libxml/parserInternals.h>

#include "xml2spumux.h"
#include "bitmap.h"
#include "vtxdecode.h"
#include "vtx15x18.h"
#include "render_freetype.h"

unsigned char ttxt_palette[27]= { 0x00,0x00,0x00,
                            0xff,0x00,0x00,
                            0x00,0xff,0x00,
                            0xff,0xff,0x00,
                            0x00,0x00,0xff,
                            0xff,0x00,0xff,
                            0x00,0xff,0xff,
                            0xff,0xff,0xff,
                            0x99,0x99,0x99};

unsigned char ttxt_trans[9]={ 0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0x00 };


static xmlEntityPtr xmlsubs_GetEntity(void *user_data, const char *name) {
  return xmlGetPredefinedEntity(name);
}

void debug_print_subtitle(xmlsubsParseState* state) {
  int row,col;

  printf("Subtitle: start=\"%s\", end=\"%s\"\n",state->start,state->end);
  for (row=1;row<=24;row++) {
    printf("   ");
    for (col=1;col<=40;col++) {
      printf("%d",col%10);
    }
    printf("\n");
    printf("%02d ",row);
    for (col=1;col<=40;col++) {
      if (state->subtitle.text[row-1][col-1]) {
        printf("%c",state->subtitle.text[row-1][col-1]);
      } else {
        printf(" ");
      }
    }
    printf("\n");
  }
}

static void xmlsubs_StartDocument(xmlsubsParseState *state) {
  state->state = PARSER_START;
}

static void xmlsubs_EndDocument(xmlsubsParseState *state) {
  state->state = PARSER_END;
}

static void xmlsubs_StartElement(xmlsubsParseState *state, const char *name,
                               const char **attrs) {
  int i;

  if (strcmp(name,"subpictures")==0) {
    state->state=PARSER_IN_SUBPICTURES;
    printf("<subpictures>");
  } else if (strcmp(name,"stream")==0) {
    state->subtitle_number=0;
    state->state=PARSER_IN_STREAM;
    printf("<stream>");
  } else if (strcmp(name,"spu")==0) {
    state->subtitle_number++;
    state->state=PARSER_IN_SPU;
    state->start[0]=0;
    state->end[0]=0;
    if (attrs!=NULL) {
      for (i=0;attrs[i]!=NULL;i+=2) {
        if (strcmp(attrs[i],"start")==0) {
          strncpy(state->start,attrs[i+1],sizeof(state->start));
        } else if (strcmp(attrs[i],"end")==0) {
          strncpy(state->end,attrs[i+1],sizeof(state->end));
        }
      }
    }
    memset(state->subtitle.text,0,sizeof(state->subtitle.text));
    memset(state->subtitle.text,0,sizeof(state->subtitle.text));
  } else if (strcmp(name,"line")==0) {
    state->state=PARSER_IN_LINE;
    state->row=1;
    state->col=1;
    if (attrs!=NULL) {
      for (i=0;attrs[i]!=NULL;i+=2) {
        if (strcmp(attrs[i],"row")==0) {
          state->row=atoi(attrs[i+1])-1;
        } else if (strcmp(attrs[i],"col")==0) {
          state->col=atoi(attrs[i+1])-1;
        }
      }
    }
  } else if (strcmp(name,"black")==0) {
    state->colour=0;
  } else if (strcmp(name,"red")==0) {
    state->colour=1;
  } else if (strcmp(name,"lime")==0) {
    state->colour=2;
  } else if (strcmp(name,"yellow")==0) {
    state->colour=3;
  } else if (strcmp(name,"blue")==0) {
    state->colour=4;
  } else if (strcmp(name,"magenta")==0) {
    state->colour=5;
  } else if (strcmp(name,"cyan")==0) {
    state->colour=6;
  } else if (strcmp(name,"white")==0) {
    state->colour=7;
  }
}

void render_subtitle_teletext(xmlsubsParseState *state) {
  bitmap_t my_bitmap;
  int row,col;
  unsigned char ch;
  int fgcolour,bgcolour;
  char filename[20];
  int font_x,font_y;
  int bitmap_x,bitmap_y;
  int x,y;
  unsigned char q;
  int i;
  int colour_used[8];

  for (i=0;i<8;i++) { colour_used[i]=0; }

  init_bitmap(&my_bitmap,720,576,8);  // 8 is used for transparency (0x999999)
  bgcolour=0; // Black background
  colour_used[bgcolour]=1;
  for (row=0;row<24;row++) {
    for (col=0;col<40;col++) {
      ch=iso2vtx[state->subtitle.text[row][col]&0xff];
      if (ch) {
        fgcolour=state->subtitle.colours[row][col];
        colour_used[fgcolour]=1;
        font_x=(ch % 32)*15;
        font_y=(ch/32)*18;
        bitmap_x=col*15+(720-(40*15))/2;
        bitmap_y=row*18+(576-(24*18))/2;

/* GIF is 480x144, and contains 32x8 characters */
/* Each character is 15x18 - we double it to 15x30*/

        for (x=0;x<15;x++) {
          for (y=0;y<36;y+=2) {
            i=480*(font_y+(y/2))+(font_x+x);
            if (i >= sizeof(vtx15x18)) {
              fprintf(stderr,"ERROR: row=%d, col=%d, size=%d, i=%d, font_x+x=%d, font_y+y=%d\n",row,col,sizeof(vtx15x18),i,font_x+x,font_y+y/2); 
              exit(1);
            }
            q=vtx15x18[i];
            if (q=='.') { q=bgcolour; } else { q=fgcolour; }
            my_bitmap.buffer[720*(bitmap_y+y)+(bitmap_x+x)]=q;
            my_bitmap.buffer[720*(bitmap_y+y+1)+(bitmap_x+x)]=q;
          }
        }
      }
    }
  }
  sprintf(filename,"sub%05d.png",state->subtitle_number);
  write_png(&my_bitmap,filename,ttxt_palette,ttxt_trans,9);

  x=1; // Transparent
  for (i=0;i<8;i++) x+=colour_used[i];

  if (x > 4) { 
    fprintf(stderr,"WARNING: %d colours needed in SPU starting at %s\n",x,state->start);
    printf("<!-- WARNING: %d colours needed in following SPU -->\n",x);
  }

  fprintf(stderr,"%s\r",state->start);
  printf("<spu");
  if (state->start[0]) { printf(" start=\"%s\"",state->start); }
  if (state->end[0]) { printf(" end=\"%s\"",state->end); }
  printf(" image=\"%s\" />\n",filename);
}

static void xmlsubs_EndElement(xmlsubsParseState *state, const char *name) {
  if (strcmp(name,"subpictures")==0) {
    state->state=PARSER_START;
    printf("</subpictures>");
  } else if (strcmp(name,"stream")==0) {
    state->state=PARSER_IN_SUBPICTURES;
    printf("</stream>");
  } else if (strcmp(name,"spu")==0) {
    state->state=PARSER_IN_STREAM;
    //    debug_print_subtitle(state);
    if (state->use_freetype) {
      render_subtitle_freetype(state);
    } else {
      render_subtitle_teletext(state);
    }
  } else if (strcmp(name,"line")==0) {
    state->state=PARSER_IN_SPU;
    state->col=1;
    state->row++;
  }
}

static void xmlsubs_Characters(xmlsubsParseState *state, const char *chars,int len) {
  int i;
  unsigned char x,y,z;
  uint16_t ud;

  if (state->state==PARSER_IN_LINE) {
    for (i=0;i<len;i++) {
      if (state->col < 40) {
        // Convert utf-8 to utf-16
        z=chars[i];
        if (z < 128) {
          ud=z;
        } else {
          i++;
          y=chars[i];
          if (z < 224) {
            ud=(z-192)*64 + (y-128);
          } else {
            i++;
            x=chars[i];
            ud=(z-224)*4096 + (y-128)*64 + (x-128);
          }
        }
        state->subtitle.text[state->row][state->col]=ud;
        state->subtitle.colours[state->row][state->col]=state->colour;
      }
      state->col++;
    }
    if (state->col>40) {
      fprintf(stderr,"ERROR IN INPUT: line too wide - \"");
      for (i=0;i<len;i++) { fprintf(stderr,"%c",chars[i]); }
      fprintf(stderr,"\"\n");
      exit(0);
    }
  }
}

static xmlSAXHandler xmlsubsSAXParser = {
   0, /* internalSubset */
   0, /* isStandalone */
   0, /* hasInternalSubset */
   0, /* hasExternalSubset */
   0, /* resolveEntity */
   (getEntitySAXFunc)xmlsubs_GetEntity, /* getEntity */
   0, /* entityDecl */
   0, /* notationDecl */
   0, /* attributeDecl */
   0, /* elementDecl */
   0, /* unparsedEntityDecl */
   0, /* setDocumentLocator */
   (startDocumentSAXFunc)xmlsubs_StartDocument, /* startDocument */
   (endDocumentSAXFunc)xmlsubs_EndDocument, /* endDocument */
   (startElementSAXFunc)xmlsubs_StartElement, /* startElement */
   (endElementSAXFunc)xmlsubs_EndElement, /* endElement */
   0, /* reference */
   (charactersSAXFunc)xmlsubs_Characters, /* characters */
   0, /* ignorableWhitespace */
   0, /* processingInstruction */
   0, /* comment */
   0, /* warning */
   0, /* error */
   0 /* fatalError */
};

int main(int argc, char* argv[]) {
  xmlParserCtxtPtr ctxt=NULL;
  xmlsubsParseState state;
  int i;

  state.fontfilename=NULL;
  state.use_freetype=0;

  fprintf(stderr,"argc=%d\n",argc);
  i=1;
  while (i < argc) {
    if (strcmp(argv[i],"-freetype")==0) {
      state.use_freetype=1;
    } else if (strcmp(argv[i],"-font")==0) {
      i++;
      state.fontfilename=argv[i];
    } else {
      if (ctxt==NULL) {
        ctxt = (xmlParserCtxtPtr)xmlCreateFileParserCtxt(argv[i]);
      }
    }
    i++;
  }

  if (ctxt == NULL) {
    fprintf(stderr,"Usage: %s [-freetype] [-font font.ttf] filename.xml\n",argv[0]);
    exit(1);
  }

  ctxt->sax = &xmlsubsSAXParser;
  ctxt->userData = &state;

  xmlParseDocument(ctxt);

  ctxt->sax = NULL;
  xmlFreeParserCtxt(ctxt);

  fprintf(stderr,"Finished!\n");
  exit(0);
}
