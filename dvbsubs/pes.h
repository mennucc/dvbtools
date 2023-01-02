#ifndef _PES_H
#define _PES_H

#include <stdint.h>

#define PESBUFSIZE 1265536
char* pts2hmsu(uint64_t pts, char sep);
uint64_t get_pes_pts (unsigned char* buf);
int read_pes_packet (int fd, uint16_t pid, uint8_t* buf, int vdrmode);

#endif
