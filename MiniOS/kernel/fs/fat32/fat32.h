#ifndef FAT32_H_
#define FAT32_H_

#include <../../../include/MiniOS/fs/fat.h>
#include <../../../include/MiniOS/vfs/vfs.h>
// #define FAT_BUF_NUM 2
// extern BUF_512 fat_buf[FAT_BUF_NUM];

u32 init_fat_info();

// u32 fs_create_with_attr(u8 *filename, u8 attr);
// u32 read_fat_sector(u32 ThisFATSecNum);

#endif