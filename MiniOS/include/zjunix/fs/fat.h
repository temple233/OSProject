#ifndef _ZJUNIX_FS_FAT_H
#define _ZJUNIX_FS_FAT_H

#include "../type.h"
#include "fscache.h"

/* 4k data buffer number in each file struct */
#define LOCAL_DATA_BUF_NUM 4

#define SECTOR_SIZE 512
#define CLUSTER_SIZE 4096

// #pragma pack(push, 1)
struct __attribute__((__packed__)) dir_entry_attr {
    u8 name[8];                   /* Name */
    u8 ext[3];                    /* Extension */
    u8 attr;                      /* attribute bits */
    u8 lcase;                     /* Case for base and extension */
    u8 ctime_cs;                  /* Creation time, centiseconds (0-199) */
    u16 ctime;                    /* Creation time */
    u16 cdate;                    /* Creation date */
    u16 adate;                    /* Last access date */
    u16 starthi;                  /* Start cluster (Hight 16 bits) */
    u16 time;                     /* Last modify time */
    u16 date;                     /* Last modify date */
    u16 startlow;                 /* Start cluster (Low 16 bits) */
    u32 size;                     /* file size (in bytes) */
};
// #pragma pack(pop)

union dir_entry {
    u8 data[32];
    struct dir_entry_attr attr;
};

/* file struct */
typedef struct fat_file {
    unsigned char path[256];
    /* Current file pointer */
    unsigned long loc;
    /* Current directory entry position */
    unsigned long dir_entry_pos;
    unsigned long dir_entry_sector;
    /* current directory entry */
    union dir_entry entry;
    /* Buffer clock head */
    unsigned long clock_head;
    /* For normal FAT32, cluster size is 4k */
    BUF_4K data_buf[LOCAL_DATA_BUF_NUM];
} FILE;

typedef struct fs_fat_dir {
    unsigned long cur_sector;
    unsigned long loc;
    unsigned long sec;
} FS_FAT_DIR;

struct __attribute__((__packed__)) BPB_attr {
    // 0x00 ~ 0x0f
    u8 jump_code[3];
    u8 oem_name[8];
    u16 sector_size;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    // 0x10 ~ 0x1f
    u8 number_of_copies_of_fat;
    u16 max_root_dir_entries;
    u16 num_of_small_sectors;
    u8 media_descriptor;
    u16 sectors_per_fat;
    u16 sectors_per_track;
    u16 num_of_heads;
    u32 num_of_hidden_sectors;
    // 0x20 ~ 0x2f
    u32 num_of_sectors;
    u32 num_of_sectors_per_fat;
    u16 flags;
    u16 version;
    u32 cluster_number_of_root_dir;
    // 0x30 ~ 0x3f
    u16 sector_number_of_fs_info;
    u16 sector_number_of_backup_boot;
    u8 reserved_data[12];
    // 0x40 ~ 0x51
    u8 logical_drive_number;
    u8 unused;
    u8 extended_signature;
    u32 serial_number;
    u8 volume_name[11];
    // 0x52 ~ 0x1fe
    u8 fat_name[8];
    u8 exec_code[420];
    u8 boot_record_signature[2];
};

union BPB_info {
    u8 data[512];
    struct BPB_attr attr;
};

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

unsigned long fs_find_fat(FILE *file);

unsigned long init_fs_fat();

unsigned long fs_open_fat(FILE *file, unsigned char *filename);

unsigned long fs_close_fat(FILE *file);

unsigned long fs_read_fat(FILE *file, unsigned char *buf, unsigned long count);

unsigned long fs_write_fat(FILE *file, const unsigned char *buf, unsigned long count);

unsigned long fs_fflush_fat();

void fs_lseek_fat(FILE *file, unsigned long new_loc);

unsigned long fs_create_fat(unsigned char *filename);

unsigned long fs_mkdir_fat(unsigned char *filename);

u32 fs_open_dir_fat(FS_FAT_DIR *dir, u8 *filename);

u32 fs_read_dir_fat(FS_FAT_DIR *dir, u8 *buf);

unsigned long fs_rm_fat(unsigned char *filename);

unsigned long fs_mv_fat(unsigned char *src, unsigned char *dest);

unsigned long fs_cat_fat(unsigned char * path);

// utils.c
u32 read_block(u8 *buf, u32 addr, u32 count);

u32 write_block(u8 *buf, u32 addr, u32 count);

u16 get_u16(u8 *ch);

u32 get_u32(u8 *ch);

void set_u16(u8 *ch, u16 num);

void set_u32(u8 *ch, u32 num);

u32 fs_wa(u32 num);

// fat32_utils.c
u32 get_start_cluster(const FILE *file);

u32 get_fat_entry_value(u32 clus, u32 *ClusEntryVal);

u32 fs_modify_fat(u32 clus, u32 ClusEntryVal);

u32 get_entry_filesize_fat(u8 *entry);

u32 get_entry_attr_fat(u8 *entry);

void get_filename(u8 *entry, u8 *buf);

void cluster_to_fat_entry(u32 clus, u32 *ThisFATSecNum, u32 *ThisFATEntOffset);
u32 fs_dataclus2sec(u32 clus);
u32 fs_sec2dataclus(u32 sec);

#endif  // !_ZJUNIX_FS_FAT_H