#ifndef _BITMAP_H
#define _BITMAP_H

/* A simple bitmap buffer.  We only need one. */

typedef struct {
  unsigned char buffer[720*576];
  int width;
  int height;
} bitmap_t;

void init_bitmap(bitmap_t* bitmap, int width, int height, int colour);
int write_png(bitmap_t* bitmap, char *file_name,unsigned char* my_palette,unsigned char* my_trans,int col_count);

#endif
