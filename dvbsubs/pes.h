#ifndef _PES_H
#define _PES_H

#include <stdint.h>

char* pts2hmsu(uint64_t pts);
uint64_t get_pes_pts (unsigned char* buf);
int read_pes_packet (int fd, uint16_t pid, uint8_t* buf, int vdrmode);

#endif
