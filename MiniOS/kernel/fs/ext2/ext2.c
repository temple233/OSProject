//
//
//
//

// fat.h also zjunix/fs/fat.h
#include <zjunix/fs/ext2.h>
#include <zjunix/fs/errno.h>
#include <zjunix/fs/err.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/log.h>
#include <intr.h>

#include "../debug.h"

static u8 filename11[13];
// 4KB == 1 << 12 == page size == cluster size == block size
static u8 new_alloc_empty[PAGE_SIZE];

static BUF_512 dir_data_buf[DIR_DATA_BUF_NUM];
static u32 dir_data_clock_head = 0;

/**
 * .data
 * .dss
 * That's OK!
 */
// for cache
struct ext2_base_information  ext2_info;
struct ext2_base_information *ext2_bi = &ext2_info;
struct ext2_super_block *ext2_sb;
/* fat buffer clock head */
static u32 fat_clock_head = 0;
BUF_512 fat_buf[FAT_BUF_NUM];

static inline int init_fs_ext2_info(u32 base)
{
    u32 err;
    // ext2 basic information
    ext2_bi = (struct ext2_base_information *)kmalloc(sizeof(struct ext2_base_information));
    if (ext2_bi == 0)
        return -ENOMEM;
    ext2_bi->ex_base = base;
    ext2_bi->ex_first_sb_sect = ext2_bi->ex_base + EXT2_BOOT_BLOCK_SECT;
    
    // ext2 super block
    // which is very same with VFS
    ext2_bi->usb.data = (u8 *)kmalloc(sizeof(u8) * EXT2_SUPER_BLOCK_SECT * SECTOR_SIZE);
    if (ext2_bi->usb.data == 0)
        return -ENOMEM;
    err = read_block(ext2_bi->usb.data, ext2_bi->ex_first_sb_sect, EXT2_SUPER_BLOCK_SECT);
    if (err)
        return -EIO;

///////////////////////////////////////////////////////////////////////////////////////////////

    ext2_sb = ext2_bi->usb.sb;

///////////////////////////////////////////////////////////////////////////////////////////////

    // find GBT in group0
    ext2_bi->ex_first_gdt_sect = base + (( EXT2_BASE_BLOCK_SIZE << ext2_bi->usb.sb->block_size) >> SECTOR_SHIFT);

    return 0;
}


static inline void init_fat_buf() {
    int i = 0;
    for (i = 0; i < FAT_BUF_NUM; i++) {
        fat_buf[i].cur = 0xffffffff;
        fat_buf[i].state = 0;
    }
}

static inline void init_dir_buf() {
    int i = 0;
    for (i = 0; i < DIR_DATA_BUF_NUM; i++) {
        dir_data_buf[i].cur = 0xffffffff;
        dir_data_buf[i].state = 0;
    }
}


unsigned long init_fs_ext2(u32 base)
{
	u32 res = init_fs_ext2_info(base);
	if(0 != res)
		goto init_fs_err;
	init_fat_buf();
	init_dir_buf();
    return 0;

init_fs_err:
	log(LOG_FAIL, "EXT2 file system init fail");
}


unsigned long fs_find_ext2(FILE_EXT2 *file)
{

}

unsigned long fs_open_ext2(FILE_EXT2 *file, unsigned char *filename)
{

}

unsigned long fs_close_ext2(FILE_EXT2 *file)
{

}

unsigned long fs_read_ext2(FILE_EXT2 *file, unsigned char *buf, unsigned long count)
{

}

unsigned long fs_write_ext2(FILE_EXT2 *file, const unsigned char *buf, unsigned long count)
{

}

unsigned long fs_fflush_ext2()
{

}

void fs_lseek_ext2(FILE_EXT2 *file, unsigned long new_loc)
{

}

unsigned long fs_create_ext2(unsigned char *filename)
{

}

unsigned long fs_mkdir_ext2(unsigned char *filename)
{

}

unsigned long fs_rm_ext2(unsigned char *filename)
{

}

unsigned long fs_mv_ext2(unsigned char *src, unsigned char *dest)
{

}

unsigned long fs_cat_ext2(unsigned char * path)
{

}
