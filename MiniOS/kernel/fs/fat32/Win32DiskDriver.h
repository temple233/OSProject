#ifndef WIN32DISKDRIVER_H_
#define WIN32DISKDRIVER_H_

#include <Windows.h>

BOOL sd_read_block(unsigned char *p, unsigned long SectorStart, unsigned long SectorCount);
BOOL sd_write_block( unsigned char *p, unsigned long SectorStart, unsigned long SectorCount);

#endif