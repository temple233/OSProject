#ifndef WIN32DISKDRIVER_H_
#define WIN32DISKDRIVER_H_

#include <Windows.h>

BOOL sd_read_block(unsigned long SectorStart, unsigned long SectorCount, unsigned char *p);
BOOL sd_write_block(unsigned long SectorStart, unsigned long SectorCount, unsigned char *p);

#endif