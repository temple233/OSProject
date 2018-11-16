#include "fat32.h"

// for debug
#include "Win32DiskDriver.h"
#include <stdio.h>

static struct fs_info fat_info; // see in MiniOS/fat.h

void printMBR(struct fs_info *fp)
{
    printf("sector_size = %u\n", fp->BPB.attr.sector_size);
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
	if(!sd_read_block(meta, 0, 1)) {
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
    
    fat_info.total_data_clusters = total_data_sectors / sectors_per_cluster;
    if (fat_info.total_data_clusters < 65525) {
        ret = 5;
    }

    // sector ID for root dirctory in this MBR/DBR
    fat_info.first_data_sector = reserved_sectors + sectors_per_fat * 2;

    sd_read_block(&(fat_info.fs_info), fat_info.base_addr + 1, 1);

    return ret;
}

void printRootDirectory()
{
	
}