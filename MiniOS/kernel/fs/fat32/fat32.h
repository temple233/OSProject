#ifndef FAT32_H_
#define FAT32_H_

#include "../../../include/zjunix/fs/fat.h"
#include "../../../include/zjunix/vfs/vfs.h"
#include "../../../include/zjunix/type.h"

#define FAT_BUF_NUM 2

// for utils.c
// from fat32.c
extern BUF_512 fat_buf[FAT_BUF_NUM];
// also ditto
extern struct fs_info fat_info;

u32 init_fs_info();
void printMBR(struct fs_info *fp);
u32 fs_next_slash(u8 *f, u8 *se_name);
u32 fs_cmp_filename(const u8 *f1, const u8 *f2);
u32 fs_create_with_attr(u8 *filename, u8 attr);
u32 read_fat_sector(u32 ThisFATSecNum);

#endif