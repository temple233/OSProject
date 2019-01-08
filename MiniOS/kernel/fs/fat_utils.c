#include <driver/sd.h>
#include "fat/fat.h"

/* DIR_FstClusHI/LO to clus */
u32 get_start_cluster(const FILE *file) {
    return (file->entry.attr.starthi << 16) + (file->entry.attr.startlow);
}

/* Get fat entry value for a cluster */
u32 get_fat_entry_value(u32 clus, u32 *ClusEntryVal) {
    u32 ThisFATSecNum;
    u32 ThisFATEntOffset;
    u32 index;

    cluster_to_fat_entry(clus, &ThisFATSecNum, &ThisFATEntOffset);

    index = read_fat_sector(ThisFATSecNum);
    if (index == 0xffffffff)
        goto get_fat_entry_value_err;

    *ClusEntryVal = get_u32(fat_buf[index].buf + ThisFATEntOffset) & 0x0FFFFFFF;

    return 0;
get_fat_entry_value_err:
    return 1;
}

/* modify fat for a cluster */
u32 fs_modify_fat(u32 clus, u32 ClusEntryVal) {
    u32 ThisFATSecNum;
    u32 ThisFATEntOffset;
    u32 fat32_val;
    u32 index;

    cluster_to_fat_entry(clus, &ThisFATSecNum, &ThisFATEntOffset);

    index = read_fat_sector(ThisFATSecNum);
    if (index == 0xffffffff)
        goto fs_modify_fat_err;

    fat_buf[index].state = 3;

    ClusEntryVal &= 0x0FFFFFFF;
    fat32_val = (get_u32(fat_buf[index].buf + ThisFATEntOffset) & 0xF0000000) | ClusEntryVal;
    set_u32(fat_buf[index].buf + ThisFATEntOffset, fat32_val);

    return 0;
fs_modify_fat_err:
    return 1;
}

/* Determine FAT entry for cluster */
// clus => one secetor of 8 this 
void cluster_to_fat_entry(u32 clus, u32 *ThisFATSecNum, u32 *ThisFATEntOffset)
{
    u32 FATOffset = clus << 2;
    *ThisFATSecNum = fat_info.BPB.attr.reserved_sectors + (FATOffset >> 9) + fat_info.base_addr;
    *ThisFATEntOffset = FATOffset & 511;// 0x01ff
}

/* data cluster num <==> sector num */
u32 fs_dataclus2sec(u32 clus)
{
    return ((clus - 2) << fs_wa(fat_info.BPB.attr.sectors_per_cluster)) + fat_info.first_data_sector;
}

u32 fs_sec2dataclus(u32 sec)
{
    return ((sec - fat_info.first_data_sector) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster)) + 2;
}


u32 get_entry_filesize_fat(u8 *entry)
{
    return get_u32(entry + 28);
}

u32 get_entry_attr_fat(u8 *entry)
{
    return entry[11];
}