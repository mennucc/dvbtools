#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "bitmap.h"
#include "render_freetype.h"

//#define FONT_FILENAME "/usr/share/fonts/truetype/Arial.ttf"
#define FONT_FILENAME "/usr/share/fonts/truetype/TiresiasScreenfont.ttf"

unsigned char palette[27]= { 0x00,0x00,0x00,
                            0xff,0x00,0x00,
                            0x00,0xff,0x00,
                            0xff,0xff,0x00,
                            0x00,0x00,0xff,
                            0xff,0x00,0xff,
                            0x00,0xff,0xff,
                            0xff,0xff,0xff,
                            0x99,0x99,0x99};

unsigned char trans[9]={ 0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0x00 };


FT_Library library=NULL;
FT_Face face;
FT_UInt glyph_index;
 
void my_draw_bitmap(bitmap_t* my_bitmap,FT_GlyphSlot slot, int x, int y,int colour) {
  int srow;
  int sp, dp, w, h;
                                                                                                                                              
  if (slot->bitmap.pixel_mode == ft_pixel_mode_mono) {
   srow=0;
   for (h = 0; h < slot->bitmap.rows ; h++, srow += slot->bitmap.pitch) {
     for (w=0, sp = dp = 0; w < slot->bitmap.width; w++, ++dp, ++sp) {
       if (slot->bitmap.buffer[srow + sp / 8] & (0x80 >> (sp % 8))) {
         my_bitmap->buffer[(my_bitmap->width*(y+h))+x+w]=colour;
       }
     }
   }
  } else {
    fprintf(stderr,"Unsupported Freetype Pixel Mode - %d\n",slot->bitmap.pixel_mode);
    exit(1);
  }
}

 
int null_render_text(uint16_t* text, int len, int pen_x, int pen_y,FT_Face face) {
  int n;
  int error;
  FT_GlyphSlot slot;
 
  slot=face->glyph;
 
  for ( n = 0; n < len; n++ ) { /* load glyph image into the slot (erase previous one) */
    error = FT_Load_Char( face, text[n], FT_LOAD_RENDER|FT_LOAD_MONOCHROME );
    if ( error ) continue; /* ignore errors */
    pen_x += slot->advance.x/64;
  }
  return(pen_x);
}

void fill_rect(bitmap_t* bitmap, int col, int x0, int y0, int x1, int y1) {
  int x;
  int y;
  unsigned char* p;
 
  for (y=y0;y<=y1;y++) {
    p=&bitmap->buffer[bitmap->width*y+x0];
    for (x=x0;x<=x1;x++) *(p++)=col;
  }
}

int render_text(bitmap_t* my_bitmap,uint16_t* text, char* colours, int len, int pen_x, int pen_y,FT_Face face) {
  int n;
  int error;
  FT_GlyphSlot slot;
  uint16_t ch;
 
  slot=face->glyph;
 
  for ( n = 0; n < len; n++ ) { /* load glyph image into the slot (erase previous one) */
    ch=text[n];
    if (ch=='#') { ch=0x266B; } 

    error = FT_Load_Char( face, ch, FT_LOAD_RENDER|FT_LOAD_MONOCHROME );
    if ( error ) {
      fprintf(stderr,"WARNING: error rendering character %04x\n",ch);
      continue; /* ignore errors */
    }
 
    /* now, draw to our target surface */
 
    my_draw_bitmap( my_bitmap, slot, pen_x+slot->bitmap_left, pen_y-slot->bitmap_top, colours[n]);
 
#if 0
    printf("char=%c\n",text[n]);
    printf("bitmap_left=%d\n",slot->bitmap_left);
    printf("bitmap_top=%d\n",slot->bitmap_top);
    printf("bitmap:rows=%d\n",slot->bitmap.rows);
    printf("bitmap:width=%d\n",slot->bitmap.width);
    printf("bitmap:pitch=%d\n",slot->bitmap.pitch);
    printf("bitmap:pixel_mode=%d, mono=%d\n",slot->bitmap.pixel_mode,ft_pixel_mode_mono);
    printf("pen-x=%d, advance.x=%d\n",pen_x,slot->advance.x);
#endif
    /* increment pen position */
    pen_x += slot->advance.x/64;
  }
  return(pen_x);
}

void render_subtitle_freetype(xmlsubsParseState *state) {
  int error;
  int pen_x,pen_y;
  int new_x;
 
  bitmap_t my_bitmap;
  int row,col;
  unsigned char ch;
  int bgcolour;
  char filename[20];
  int i;
  int colour_used[8];
  int first_char,last_char;
  int left_margin,right_margin;

  if (library==NULL) {
    error = FT_Init_FreeType( &library );
    if (error) { printf("Error initialising library - %d\n",error); exit(0); }

    if (state->fontfilename==NULL) {
      state->fontfilename=FONT_FILENAME;
    }
    error = FT_New_Face( library, state->fontfilename, 0, &face );
    if ( error == FT_Err_Unknown_File_Format ) {
      printf("Error opening font file - unsupported format\n");
      exit(0);
    } else if (error) {
      printf("Error opening font file\n");
      exit(0);
    }

    error = FT_Set_Char_Size(face,1860,1920,0,0);
    if (error) { printf("Error setting char size - %d\n",error); exit(0); }
  }

  for (i=0;i<8;i++) { colour_used[i]=0; }

  init_bitmap(&my_bitmap,720,576,8);  // 8 is used for transparency (0x999999)

  bgcolour=0; // Black background
  colour_used[bgcolour]=1;
  for (row=0;row<24;row++) {
    first_char=-1;
    last_char=-1;

    for (col=0;col<40;col++) {
      // to do: map this to the right charset
      ch=state->subtitle.text[row][col];
      if (ch!=0) {
        if (first_char==-1) { first_char=col; }
        last_char=col;
      }
    }
    if (first_char!=-1) {
      left_margin=first_char;
      right_margin=39-last_char;

      pen_x=0;
      new_x=null_render_text(&(state->subtitle.text[row][first_char]),
                        last_char-first_char+1,
                        pen_x,pen_y,
                        face);

      if (abs(left_margin-right_margin) <= 3) { // CENTRE TEXT
        pen_x=(720-(new_x-pen_x))/2;
      } else { 
        pen_x=88;
        fprintf(stderr,"LEFT-ALIGNING LINE %d IN SUBTITLE %d - length=%d\n",row,state->subtitle_number,last_char-first_char+1);
      }
      pen_y=(row*18)+92+18;

      fill_rect(&my_bitmap,0,pen_x-6,pen_y,pen_x+new_x+6,pen_y+36);
      pen_x=render_text(&my_bitmap,
                        &(state->subtitle.text[row][first_char]),
                        &(state->subtitle.colours[row][first_char]),
                        last_char-first_char+1,
                        pen_x,pen_y+26,
                        face);
     }
 
  }
  sprintf(filename,"sub%05d.png",state->subtitle_number);
  write_png(&my_bitmap,filename,palette,trans,9);

  /*  x=1; // Transparent
  for (i=0;i<8;i++) x+=colour_used[i];

  if (x > 4) { 
    fprintf(stderr,"WARNING: %d colours needed in SPU starting at %s\n",x,state->start);
    printf("<!-- WARNING: %d colours needed in following SPU -->\n",x);
  }
  */

  fprintf(stderr,"%s\r",state->start);
  printf("<spu");
  if (state->start[0]) { printf(" start=\"%s\"",state->start); }
  if (state->end[0]) { printf(" end=\"%s\"",state->end); }
  printf(" image=\"%s\" />\n",filename);
}

