#include "fat32.h"
#include "utils.h"
#include "../../../include/driver/sd.h"
// for debug
#include "Win32DiskDriver.h"
#include <stdio.h>

/* fat buffer clock head */
u32 fat_clock_head = 0;
BUF_512 fat_buf[FAT_BUF_NUM];

u8 new_alloc_empty[PAGE_SIZE];

#define DIR_DATA_BUF_NUM 4
BUF_512 dir_data_buf[DIR_DATA_BUF_NUM];
u32 dir_data_clock_head = 0;

// see in MiniOS/fat.h
// must not be static
// also for utils.c
// from fat32.c
struct fs_info fat_info;

void printMBR(struct fs_info *fp)
{
    printf("DBR base addr == %lu\n", fp->base_addr);
    printf("sector_size = %hu\n", fp->BPB.attr.sector_size);
    printf("sectors_per_cluster = %u\n", fp->BPB.attr.sectors_per_cluster);
    printf("reserved_sectors = %u\n", fp->BPB.attr.reserved_sectors);
    printf("number_of_copies_of_fat = %u\n", fp->BPB.attr.number_of_copies_of_fat);
    printf("max_root_dir_entries = %u\n", fp->BPB.attr.max_root_dir_entries);
    printf("num_of_small_sectors = %u\n", fp->BPB.attr.num_of_small_sectors);
    printf("media_descriptor = %u\n", fp->BPB.attr.media_descriptor);
    printf("sectors_per_fat = %u\n", fp->BPB.attr.sectors_per_fat);
    printf("sectors_per_track = %u\n", fp->BPB.attr.sectors_per_track);
    printf("num_of_heads = %u\n", fp->BPB.attr.num_of_heads);
    printf("num_of_hidden_sectors = %u\n", fp->BPB.attr.num_of_hidden_sectors);
    printf("num_of_sectors = %u\n", fp->BPB.attr.num_of_sectors);
    printf("num_of_sectors_per_fat = %u\n", fp->BPB.attr.num_of_sectors_per_fat);
    printf("flags = %u\n", fp->BPB.attr.flags);
    printf("version = %u\n", fp->BPB.attr.version);
    printf("cluster_number_of_root_dir = %u\n", fp->BPB.attr.cluster_number_of_root_dir);
    printf("sector_number_of_fs_info = %u\n", fp->BPB.attr.sector_number_of_fs_info);
    printf("sector_number_of_backup_boot = %u\n", fp->BPB.attr.sector_number_of_backup_boot);
    printf("logical_drive_number = %u\n", fp->BPB.attr.logical_drive_number);
    printf("unused = %u\n", fp->BPB.attr.unused);
    printf("extended_signature = %u\n", fp->BPB.attr.extended_signature);
    printf("serial_number = %u\n", fp->BPB.attr.serial_number);
    printf("volume_name[11] = %s\n", fp->BPB.attr.volume_name);
    printf("fat_name[8] = %s\n", fp->BPB.attr.fat_name);
    // printf("exec_code[420] = %u\n", fp->BPB.attr.exec_code[420]);
    // printf("boot_record_signature[2] = %#X\n", fp->BPB.attr.boot_record_signature[2]);
}

/*
struct fs_info {
    u32 base_addr;
    u32 sectors_per_fat;
    u32 total_sectors;
    u32 total_data_clusters;
    u32 total_data_sectors;
    u32 first_data_sector;
    union BPB_info BPB;
    u8 fat_fs_info[SECTOR_SIZE];
};
*/


/*<? 0 success ?>*/
/*<? other fail ?>*/
u32 init_fs_info()
{
	u32 ret = 0;
	unsigned char meta_buf[SECTOR_SIZE];

	// MBR
	if(!sd_read_block(meta_buf, 0, 1)) {
		ret = 1;
	}

	// MBR0 -> first sector of DBR
	fat_info.base_addr = *(int *)(meta_buf + 446 + 8);

	// DBR
	if(!sd_read_block(fat_info.BPB.data, fat_info.base_addr, 1)) {
		ret = 2;
	}

    if (fat_info.BPB.attr.max_root_dir_entries != 0) {
        ret = 3;
    }
    
    if (fat_info.BPB.attr.sectors_per_fat != 0) {
        ret = 3;
    }
   
    if (fat_info.BPB.attr.num_of_small_sectors != 0) {
        ret = 4;
    }
    u32 total_sectors = fat_info.BPB.attr.num_of_sectors;
    
    u32 reserved_sectors = fat_info.BPB.attr.reserved_sectors;
    
    u32 sectors_per_fat = fat_info.BPB.attr.num_of_sectors_per_fat;
    
    // FAT + FAT backup + received == total sector
    // only data area only => cluster
    fat_info.total_data_sectors = total_sectors - reserved_sectors - sectors_per_fat * 2;
    
    u8 sectors_per_cluster = fat_info.BPB.attr.sectors_per_cluster;
    
    fat_info.total_data_clusters = fat_info.total_data_sectors / sectors_per_cluster;
    if (fat_info.total_data_clusters < 65525) {
        ret = 5;
    }

    // sector ID for root dirctory in this MBR/DBR
    fat_info.first_data_sector = reserved_sectors + sectors_per_fat * 2;

    sd_read_block(&(fat_info.fat_fs_info), fat_info.base_addr + 1, 1);

    return ret;
}
/* strcmp */
u32 fs_cmp_filename(const u8 *f1, const u8 *f2)
{
  u32 i;
  for (i = 0; i < 11; i++) {
    if (f1[i] != f2[i])
      return 1;
  }

  return 0;
}

// path parser, must be null terminated
// se_name.length == 8+3(0x20 padding)
/* like name/ or name */
/* 			  		  */
u32 fs_next_slash(u8 *f, u8 *se_name)
{
	u32 i, j, k;
    u8 chr11[13];
    for (i = 0; (*(f + i) != 0) && (*(f + i) != '/'); i++)
        ;

    for (j = 0; j < 12; j++) {
        chr11[j] = 0;
        se_name[j] = 0x20;
    }
    for (j = 0; j < 12 && j < i; j++) {
        chr11[j] = *(f + j);
        if (chr11[j] >= 'a' && chr11[j] <= 'z')
            chr11[j] = (u8)(chr11[j] - 'a' + 'A');
    }
    chr11[12] = '\000';

    for (j = 0; (chr11[j] != 0) && (j < 12); j++) {
        if (chr11[j] == '.')
            break;

        se_name[j] = chr11[j];
    }

    if (chr11[j] == '.') {
        j++;
        for (k = 8; (chr11[j] != 0) && (j < 12) && (k < 11); j++, k++) {
            se_name[k] = chr11[j];
        }
    }

    se_name[11] = '\000';

    return i;
}

// 0 no error find it
// 1 no error not find i4
// other error

/* file will be compete
// dir_entry for self => cluster
// it in which sector
// it in which 32B
// parent dir_data_buffer, may be invalid
*/
u32 fs_find(_FILE_ *file)
{
    u8 *f = file->path;
    u32 next_slash;
    u32 i, k;
    u32 next_clus;
    u32 index;
    u32 sec;

    u8 se_name[11];

    if (*(f++) != '/')
        return 2;

    index = fs_read_512(dir_data_buf, fs_dataclus2sec(2), &dir_data_clock_head, DIR_DATA_BUF_NUM);
    /* Open root directory */
    if (index == 0xffffffff)
        return 3;

    /* Find directory entry */
    while (1) {
        file->dir_entry_pos = 0xFFFFFFFF;

        next_slash = fs_next_slash(f, se_name);

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
                    if ((fs_cmp_filename(dir_data_buf[index].buf + i, se_name) == 0) &&
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

                    // first sector of this cluster
                    // return it
                    // for fs_sec2cluster();
                    if (get_fat_entry_value(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1, &next_clus) == 1)
                        goto fs_find_err;

                    // also 0x0fffffff
                    // equivalent the followings
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
        } else {
          goto fs_find_err;
        }
    }
fs_find_ok:
    return 0;
fs_find_err:
    return 1;
}


/* Write current fat sector */
u32 write_fat_sector(u32 index) {
  if ((fat_buf[index].cur != 0xffffffff) && (((fat_buf[index].state) & 0x02) != 0)) {
    /* Write FAT and FAT copy */
    if (write_block(fat_buf[index].buf, fat_buf[index].cur, 1) == 1)
      goto write_fat_sector_err;
    if (write_block(fat_buf[index].buf, fat_info.BPB.attr.num_of_sectors_per_fat + fat_buf[index].cur, 1) == 1)
      goto write_fat_sector_err;
    fat_buf[index].state &= 0x01;
  }
  return 0;
  write_fat_sector_err:
  return 1;
}

/* Read fat sector */
u32 read_fat_sector(u32 ThisFATSecNum) {
  u32 index;
  /* try to find in buffer */
  for (index = 0; (index < FAT_BUF_NUM) && (fat_buf[index].cur != ThisFATSecNum); index++)
    ;

  /* if not in buffer, find victim & replace, otherwise set reference bit */
  if (index == FAT_BUF_NUM) {
    index = fs_victim_512(fat_buf, &fat_clock_head, FAT_BUF_NUM);

    if (write_fat_sector(index) == 1)
      goto read_fat_sector_err;

    if (read_block(fat_buf[index].buf, ThisFATSecNum, 1) == 1)
      goto read_fat_sector_err;

    fat_buf[index].cur = ThisFATSecNum;
    fat_buf[index].state = 1;
  } else
    fat_buf[index].state |= 0x01;

  return index;
  read_fat_sector_err:
  return 0xffffffff;
}
