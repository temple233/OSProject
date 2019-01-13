//
//
//
//

#if !defined(EXT2_DEBUG)
#include "../fat/fat.h"
#else
#include <zjunix/fs/ext2.h>
#endif

#include <zjunix/fs/ext2.h>
#include <zjunix/fs/errno.h>
#include <zjunix/fs/err.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/log.h>
#include <intr.h>

#include "../debug.h"

#define DIR_DATA_BUF_NUM 4

static u8 filename11[13];
// 4KB == 1 << 12 == page size == cluster size == block size
static u8 new_alloc_empty[PAGE_SIZE];

static BUF_512 dir_data_buf[DIR_DATA_BUF_NUM];
static u32 dir_data_clock_head = 0;

// also for cache
extern struct fs_info fat_info;

/**
 * .data
 * .dss
 * That's OK!
 */
// for cache
struct ext2_base_information  ext2_info;
struct ext2_base_information *ext2_bi = &ext2_info;
struct ext2_super_block *ext2_sb;

/** 
 * fat buffer clock head
 */
static u32 fat_clock_head = 0;

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


/* strcmp */
static inline u32 fs_cmp_filename(const u8 *f1, const u8 *f2) {
    u32 i;
    for (i = 0; i < 11; i++) {
        if (f1[i] != f2[i])
            return 1;
    }

    return 0;
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


unsigned long fs_dump_ext2()
{
	kernel_printf("inode_num = %d\n", ext2_sb->inode_num);
	kernel_printf("block_num = %d\n", ext2_sb->block_num);
	kernel_printf("res_block_num = %d\n", ext2_sb->res_block_num);
	kernel_printf("free_block_num = %d\n", ext2_sb->free_block_num);
	kernel_printf("free_inode_num = %d\n", ext2_sb->free_inode_num);
	kernel_printf("first_data_block_no = %d\n", ext2_sb->first_data_block_no);
	kernel_printf("block_size = %d\n", ext2_sb->block_size);
	kernel_printf("slice_size = %d\n", ext2_sb->slice_size);
	kernel_printf("blocks_per_group = %d\n", ext2_sb->blocks_per_group);
	kernel_printf("slices_per_group = %d\n", ext2_sb->slices_per_group);
	kernel_printf("inodes_per_group = %d\n", ext2_sb->inodes_per_group);
	kernel_printf("install_time = %d\n", ext2_sb->install_time);
	kernel_printf("last_write_in = %d\n", ext2_sb->last_write_in);
	kernel_printf("install_count = %d\n", ext2_sb->install_count);
	kernel_printf("max_install_count = %d\n", ext2_sb->max_install_count);
	kernel_printf("magic = %d\n", ext2_sb->magic);
	kernel_printf("state = %d\n", ext2_sb->state);
	kernel_printf("err_action = %d\n", ext2_sb->err_action);
	kernel_printf("edition_change_mark = %d\n", ext2_sb->edition_change_mark);
	kernel_printf("last_check = %d\n", ext2_sb->last_check);
	kernel_printf("max_check_interval = %d\n", ext2_sb->max_check_interval);
	kernel_printf("operating_system = %d\n", ext2_sb->operating_system);
	kernel_printf("edition_mark = %d\n", ext2_sb->edition_mark);
	kernel_printf("uid = %d\n", ext2_sb->uid);
	kernel_printf("gid = %d\n", ext2_sb->gid);
	kernel_printf("first_inode = %d\n", ext2_sb->first_inode);
	kernel_printf("inode_size = %d\n", ext2_sb->inode_size);

	// success
	return 0;
}

unsigned long fs_find_ext2(FILE_EXT2 *file)
{
    u8 *f = file->path;
    u32 next_slash;
    u32 i, k;
    u32 next_clus;
    u32 index;
    u32 sec;

    if (*(f++) != '/')
        goto fs_find_err;

    index = fs_read_512(dir_data_buf, fs_dataclus2sec(2), &dir_data_clock_head, DIR_DATA_BUF_NUM);
    /* Open root directory */
    if (index == 0xffffffff)
        goto fs_find_err;

    /* Find directory entry */
    while (1) {
        file->dir_entry_pos = 0xFFFFFFFF;

        next_slash = fs_next_slash(f, filename11);

        while (1) {
            for (sec = 1; sec <= fat_info.BPB.attr.sectors_per_cluster; sec++) {
                /* Find directory entry in current cluster */
                for (i = 0; i < 512; i += 32) {
                    if (*(dir_data_buf[index].buf + i) == 0)
                        goto after_fs_find;

                    /* Ignore long path */
                    // [12] == attribute of directory entry
                    // 0x08 / 0x0f xxxx0000 - xxxx0101
                    // SHIT of C. S. I. G.
                    // FUCK FUCK FUCK FUCK FUCK FUCK
                    if ((fs_cmp_filename(dir_data_buf[index].buf + i, filename11) == 0) &&
                        ((*(dir_data_buf[index].buf + i + 11) & 0x08) == 0)) {
                        file->dir_entry_pos = i;
                        // refer to the issue in fs_close()
                        file->dir_entry_sector = dir_data_buf[index].cur - fat_info.base_addr;

                        for (k = 0; k < 32; k++)
                            file->entry.data[k] = *(dir_data_buf[index].buf + i + k);

                        goto after_fs_find;
                    }
                }
                /* next sector in current cluster */
                if (sec < fat_info.BPB.attr.sectors_per_cluster) {
                    index = fs_read_512(dir_data_buf, dir_data_buf[index].cur + 1, &dir_data_clock_head, DIR_DATA_BUF_NUM);
                    if (index == 0xffffffff)
                        goto fs_find_err;
                } else {
                    /* Read next cluster of current directory */
                    if (get_fat_entry_value(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1, &next_clus) == 1)
                        goto fs_find_err;

                    if (next_clus <= fat_info.total_data_clusters + 1) {
                        index = fs_read_512(dir_data_buf, fs_dataclus2sec(next_clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
                        if (index == 0xffffffff)
                            goto fs_find_err;
                    } else
                        goto after_fs_find;
                }
            }
        }

    after_fs_find:
        /* If not found */
        if (file->dir_entry_pos == 0xFFFFFFFF)
            goto fs_find_ok;

        /* If path parsing completes */
        if (f[next_slash] == 0)
            goto fs_find_ok;

        /* If not a sub directory */
        if ((file->entry.data[11] & 0x10) == 0)
            goto fs_find_err;

        f += next_slash + 1;

        /* Open sub directory, high word(+20), low word(+26) */
        next_clus = get_start_cluster(file);

        if (next_clus <= fat_info.total_data_clusters + 1) {
            index = fs_read_512(dir_data_buf, fs_dataclus2sec(next_clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
            if (index == 0xffffffff)
                goto fs_find_err;
        } else
            goto fs_find_err;
    }
fs_find_ok:
    return 0;
fs_find_err:
    return 1;
}

unsigned long fs_open_ext2(FILE_EXT2 *file, unsigned char *filename)
{
    u32 i;

    /* Local buffer initialize */
    for (i = 0; i < LOCAL_DATA_BUF_NUM; i++) {
        file->data_buf[i].cur = 0xffffffff;
        file->data_buf[i].state = 0;
    }

    file->clock_head = 0;

    for (i = 0; i < 256; i++)
        file->path[i] = 0;
    for (i = 0; i < 256 && filename[i] != 0; i++)
        file->path[i] = filename[i];

    file->loc = 0;

    if (fs_find_fat(file) == 1)
        goto fs_open_err;

    /* If file not exists */
    if (file->dir_entry_pos == 0xFFFFFFFF)
        goto fs_open_err;

    return 0;
fs_open_err:
    return 1;
}
/* fflush, write global buffers to sd */
u32 fs_fflush_fat() {
    u32 i;

    // FSInfo shoud add base_addr
    if (write_block(fat_info.fat_fs_info, 1 + fat_info.base_addr, 1) == 1)
        goto fs_fflush_err;

    if (write_block(fat_info.fat_fs_info, 7 + fat_info.base_addr, 1) == 1)
        goto fs_fflush_err;

    for (i = 0; i < FAT_BUF_NUM; i++)
        if (write_fat_sector(i) == 1)
            goto fs_fflush_err;

    for (i = 0; i < DIR_DATA_BUF_NUM; i++)
        if (fs_write_512(dir_data_buf + i) == 1)
            goto fs_fflush_err;

    return 0;

fs_fflush_err:
    return 1;
}

unsigned long fs_close_ext2(FILE_EXT2 *file)
{
    u32 i;
    u32 index;

    /* Write directory entry */
    index = fs_read_512(dir_data_buf, file->dir_entry_sector, &dir_data_clock_head, DIR_DATA_BUF_NUM);
    if (index == 0xffffffff)
        goto fs_close_err;

    dir_data_buf[index].state = 3;

    // Issue: need file->dir_entry to be local partition offset
    for (i = 0; i < 32; i++)
        *(dir_data_buf[index].buf + file->dir_entry_pos + i) = file->entry.data[i];
    /* do fflush to write global buffers */
    if (fs_fflush_fat() == 1)
        goto fs_close_err;
    /* write local data buffer */
    for (i = 0; i < LOCAL_DATA_BUF_NUM; i++)
        if (fs_write_4k(file->data_buf + i) == 1)
            goto fs_close_err;

    return 0;
fs_close_err:
    return 1;
}

unsigned long fs_read_ext2(FILE_EXT2 *file, unsigned char *buf, unsigned long count)
{
    u32 start_clus, start_byte;
    u32 end_clus, end_byte;
    u32 filesize = file->entry.attr.size;
    u32 clus = get_start_cluster(file);
    u32 next_clus;
    u32 i;
    u32 cc;
    u32 index;

#ifdef FS_DEBUG
    kernel_printf("fs_read: count %d\n", count);
    disable_interrupts();
#endif  // ! FS_DEBUG
    /* If file is empty */
    if (clus == 0)
        return 0;

    /* If loc + count > filesize, only up to EOF will be read */
    if (file->loc + count > filesize)
        count = filesize - file->loc;

    /* If read 0 byte */
    if (count == 0)
        return 0;

    start_clus = file->loc >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    start_byte = file->loc & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);
    end_clus = (file->loc + count - 1) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    end_byte = (file->loc + count - 1) & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);

#ifdef FS_DEBUG
    kernel_printf("start cluster: %d\n", start_clus);
    kernel_printf("start byte: %d\n", start_byte);
    kernel_printf("end cluster: %d\n", end_clus);
    kernel_printf("end byte: %d\n", end_byte);
#endif  // ! FS_DEBUG
    /* Open first cluster to read */
    for (i = 0; i < start_clus; i++) {
        if (get_fat_entry_value(clus, &next_clus) == 1)
            goto fs_read_err;

        clus = next_clus;
    }

    cc = 0;
    while (start_clus <= end_clus) {
        index = fs_read_4k(file->data_buf, fs_dataclus2sec(clus), &(file->clock_head), LOCAL_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_read_err;

        /* If in same cluster, just read */
        if (start_clus == end_clus) {
            for (i = start_byte; i <= end_byte; i++)
                buf[cc++] = file->data_buf[index].buf[i];
            goto fs_read_end;
        }
        /* otherwise, read clusters one by one */
        else {
            for (i = start_byte; i < (fat_info.BPB.attr.sectors_per_cluster << 9); i++)
                buf[cc++] = file->data_buf[index].buf[i];

            start_clus++;
            start_byte = 0;

            if (get_fat_entry_value(clus, &next_clus) == 1)
                goto fs_read_err;

            clus = next_clus;
        }
    }
fs_read_end:

#ifdef FS_DEBUG
    kernel_printf("fs_read: count %d\n", count);
    enable_interrupts();
#endif  // ! FS_DEBUG
    /* modify file pointer */
    file->loc += count;
    return cc;
fs_read_err:
    return 0xFFFFFFFF;
}

unsigned long fs_write_ext2(FILE_EXT2 *file, const unsigned char *buf, unsigned long count)
{
    /* If write 0 bytes */
    if (count == 0) {
        return 0;
    }

    u32 start_clus = file->loc >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    u32 start_byte = file->loc & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);
    u32 end_clus = (file->loc + count - 1) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    u32 end_byte = (file->loc + count - 1) & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);

    /* If file is empty, alloc a new data cluster */
    u32 curr_cluster = get_start_cluster(file);
    if (curr_cluster == 0) {
        if (fs_alloc(&curr_cluster) == 1) {
            goto fs_write_err;
        }
        file->entry.attr.starthi = (u16)(((curr_cluster >> 16) & 0xFFFF));
        file->entry.attr.startlow = (u16)((curr_cluster & 0xFFFF));
        if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(curr_cluster)) == 1)
            goto fs_write_err;
    }

    /* Open first cluster to read */
    u32 next_cluster;
    for (u32 i = 0; i < start_clus; i++) {
        if (get_fat_entry_value(curr_cluster, &next_cluster) == 1)
            goto fs_write_err;

        /* If this is the last cluster in file, and still need to open next
         * cluster, just alloc a new data cluster */
        if (next_cluster > fat_info.total_data_clusters + 1) {
            if (fs_alloc(&next_cluster) == 1)
                goto fs_write_err;

            if (fs_modify_fat(curr_cluster, next_cluster) == 1)
                goto fs_write_err;

            if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(next_cluster)) == 1)
                goto fs_write_err;
        }

        curr_cluster = next_cluster;
    }

    u32 cc = 0;
    u32 index = 0;
    while (start_clus <= end_clus) {
        index = fs_read_4k(file->data_buf, fs_dataclus2sec(curr_cluster), &(file->clock_head), LOCAL_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_write_err;

        file->data_buf[index].state = 3;

        /* If in same cluster, just write */
        if (start_clus == end_clus) {
            for (u32 i = start_byte; i <= end_byte; i++)
                file->data_buf[index].buf[i] = buf[cc++];
            goto fs_write_end;
        }
        /* otherwise, write clusters one by one */
        else {
            for (u32 i = start_byte; i < (fat_info.BPB.attr.sectors_per_cluster << 9); i++)
                file->data_buf[index].buf[i] = buf[cc++];

            start_clus++;
            start_byte = 0;

            if (get_fat_entry_value(curr_cluster, &next_cluster) == 1)
                goto fs_write_err;

            /* If this is the last cluster in file, and still need to open next
             * cluster, just alloc a new data cluster */
            if (next_cluster > fat_info.total_data_clusters + 1) {
                if (fs_alloc(&next_cluster) == 1)
                    goto fs_write_err;

                if (fs_modify_fat(curr_cluster, next_cluster) == 1)
                    goto fs_write_err;

                if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(next_cluster)) == 1)
                    goto fs_write_err;
            }

            curr_cluster = next_cluster;
        }
    }

fs_write_end:

    /* update file size */
    if (file->loc + count > file->entry.attr.size)
        file->entry.attr.size = file->loc + count;

    /* update location */
    file->loc += count;

    return cc;
fs_write_err:
    return 0xFFFFFFFF;
}

unsigned long fs_fflush_ext2()
{
    u32 i;

    // FSInfo shoud add base_addr
    if (write_block(fat_info.fat_fs_info, 1 + fat_info.base_addr, 1) == 1)
        goto fs_fflush_err;

    if (write_block(fat_info.fat_fs_info, 7 + fat_info.base_addr, 1) == 1)
        goto fs_fflush_err;

    for (i = 0; i < FAT_BUF_NUM; i++)
        if (write_fat_sector(i) == 1)
            goto fs_fflush_err;

    for (i = 0; i < DIR_DATA_BUF_NUM; i++)
        if (fs_write_512(dir_data_buf + i) == 1)
            goto fs_fflush_err;

    return 0;

fs_fflush_err:
    return 1;
}

void fs_lseek_ext2(FILE_EXT2 *file, unsigned long new_loc)
{
    u32 filesize = file->entry.attr.size;

    if (new_loc < filesize)
        file->loc = new_loc;
    else
        file->loc = filesize;
}

unsigned long fs_create_ext2(unsigned char *filename)
{

}
