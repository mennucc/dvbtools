#ifndef _XML2SPUMUX_H
#define _XML2SPUMUX_H

#include <stdint.h>

typedef struct {
  uint16_t text[25][40];
  unsigned char colours[25][40];
} subtitle_t;

typedef enum {
  PARSER_START,
  PARSER_IN_SUBPICTURES,
  PARSER_IN_STREAM,
  PARSER_IN_SPU,
  PARSER_IN_LINE,
  PARSER_END
} ParserState;

typedef struct _xmlsubsParseState {
  ParserState state;
  int colour;
  int row,col;
  char start[20];
  char end[20];
  int subtitle_number;
  subtitle_t subtitle;
  int use_freetype;
  char* fontfilename;
} xmlsubsParseState;

#endif
