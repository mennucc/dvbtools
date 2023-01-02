#include <stdlib.h>
#include <png.h>

#include <string.h>
#include <setjmp.h>
 
#include "bitmap.h"

void init_bitmap(bitmap_t* bitmap, int width, int height, int colour) {
  bitmap->width=width;
  bitmap->height=height;
  memset(bitmap->buffer,colour,sizeof(bitmap->buffer));
}

/* write_png function based on the version by Henry Mason that he
   added to vobtosub - http://gwondaleya.free.fr/vobtosub.tar.gz */

int write_png(bitmap_t* bitmap, char *file_name,unsigned char* my_palette,unsigned char* my_trans,int col_count) {
    unsigned int a,lxd;
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep row_pointers[576];

    fp = fopen(file_name, "wb");
    if(!fp) {
        fprintf(stderr,"error, unable to open/create file: %s\n",file_name);
        exit(1);
    }
    if(bitmap->width&7) {
      lxd=bitmap->width+8-(bitmap->width%8); 
    } else {
      lxd=bitmap->width;
    }
    
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr)
        return -1;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr,(png_infopp)NULL);
        return -1;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return -1;
    }

    png_init_io(png_ptr, fp);

    /* turn on or off filtering, and/or choose specific filters */
    png_set_filter(png_ptr, 0, PNG_FILTER_NONE);

    /* set the zlib compression level */
    png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

    /* set other zlib parameters */
    png_set_compression_mem_level(png_ptr, 8);
    png_set_compression_strategy(png_ptr, Z_DEFAULT_STRATEGY);
    png_set_compression_window_bits(png_ptr, 15);
    png_set_compression_method(png_ptr, 8);

    png_set_IHDR(png_ptr, info_ptr, bitmap->width, bitmap->height,
                 8,PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_set_PLTE(png_ptr,info_ptr,(png_colorp)my_palette,col_count);
    png_set_tRNS(png_ptr,info_ptr,(png_bytep)my_trans,col_count,NULL);

    png_write_info(png_ptr, info_ptr);

    png_set_packing(png_ptr);

    for (a=0; a<bitmap->height; a++) {
        row_pointers[a] = bitmap->buffer + (a * bitmap->width);
    }

    png_write_image(png_ptr, row_pointers);
    
    png_write_end(png_ptr, info_ptr);

    png_destroy_write_struct(&png_ptr, &info_ptr);

    if (fp) fclose(fp);

    return 0;
}
