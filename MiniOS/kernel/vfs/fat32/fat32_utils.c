#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/fat32.h>
#include <zjunix/vfs/vfs.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/utils.h>

// dest
// src
// mode == system reserved byte in an entry
// FAT32_NAME_NORMAL_TO_SPECIFIC 12[.] -> 11[]
// FAT32_NAME_SPECIFIC_TO_NORMAL 11[] -> 12[.]
void fat32_convert_filename(struct qstr* dest, const struct qstr* src, u8 mode, u32 direction)
{
    u8* name;
    int i;
    u32 j;
    u32 dot;
    int end;
    u32 null;
    int dot_pos;

    dest->name = 0;
    dest->len = 0;

    // 12[.] -> 11[]
    if ( direction == FAT32_NAME_NORMAL_TO_SPECIFIC ){
        u8 *name = (u8 *)kmalloc(MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8));

        // find “.”
        dot = 0;
        dot_pos = INF;
        for ( i = 0; i < src->len; i++ )
            if ( src->name[i] == '.' ){
                dot = 1;
                break;
            }
                
        if (dot)
            dot_pos = i;

        // before “.”
        if ( dot_pos > MAX_FAT32_SHORT_FILE_NAME_BASE_LEN )
            end = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN - 1;
        else
            end = dot_pos - 1;

        for ( i = 0; i < MAX_FAT32_SHORT_FILE_NAME_BASE_LEN; i++ ){
            if ( i > end )
                name[i] = '\0';
            else {
                if ( src->name[i] <= 'z' && src->name[i] >= 'a' )
                    name[i] = src->name[i] - 'a' + 'A';
                else
                    name[i] = src->name[i];
            }
        }

        // after "."
        for ( i = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN, j = dot_pos + 1; i < MAX_FAT32_SHORT_FILE_NAME_LEN; i++, j++ )
        {
            if ( j >= src->len )
                name[i] == '\0';
            else{
                if ( src->name[j] <= 'z' && src->name[j] >= 'a' )
                    name[i] = src->name[j] - 'a' + 'A';
                else
                    name[i] = src->name[j];
            }
        }
        
        dest->name = name;
        dest->len = MAX_FAT32_SHORT_FILE_NAME_LEN;
    }

    // 11[] -> 12[.]
    else if ( direction == FAT32_NAME_SPECIFIC_TO_NORMAL ) {
        null = 0;
        dot_pos = MAX_FAT32_SHORT_FILE_NAME_LEN;
        for ( i = MAX_FAT32_SHORT_FILE_NAME_LEN - 1; i  ; i-- ){
            if ( src->name[i] == 0x20 ) {
                dot_pos = i;
                null ++;
            }

        }

        dest->len = MAX_FAT32_SHORT_FILE_NAME_LEN - null;
        name = (u8 *)kmalloc((dest->len + 2) * sizeof(u8));     // 空字符 + '.'(不一定有)
        
        if ( dot_pos > MAX_FAT32_SHORT_FILE_NAME_BASE_LEN )
            dot_pos = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN;
        
        // before "."
        for ( i = 0; i < dot_pos; i++ ) {
            if (src->name[i] <= 'z' && src->name[i] >= 'a' && (mode == 0x10 || mode == 0x00) )
                name[i] = src->name[i] - 'a' + 'A';
            else if (src->name[i] <= 'Z' && src->name[i] >= 'A' && (mode == 0x18 || mode == 0x08) )
                name[i] = src->name[i] - 'A' + 'a';
            else
                name[i] = src->name[i];
        }
        
        // after "."
        i = dot_pos;
        j = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN;
        if (src->name[j] != 0x20){
            name[i] = '.';
            for ( i = dot_pos + 1; j < MAX_FAT32_SHORT_FILE_NAME_LEN && src->name[j] != 0x20; i++, j++ ){
                if (src->name[j] <= 'z' && src->name[j] >= 'a' && (mode == 0x08 || mode == 0x00) )
                    name[i] = src->name[j] - 'a' + 'A';
                else if (src->name[j] <= 'Z' && src->name[j] >= 'A' && (mode == 0x18 || mode == 0x10))
                    name[i] = src->name[j] - 'A' + 'a';
                else
                    name[i] = src->name[j];
            }
            dest->len += 1;
        }
        
        name[i] = '\000';
        dest->name = name;
    }
   
    return;
}

/**
 * read it
 */
u32 read_fat(struct inode* inode, u32 index)
{
    u8 buffer[SECTOR_SIZE];

    struct fat32_basic_information * FAT32_bi = (struct fat32_basic_information *)(inode->i_sb->s_fs_info);
    u32 base_sect = FAT32_bi->fa_FAT->base;

    // 512B / 4B for a entry
    u32 shift = SECTOR_SHIFT - FAT32_FAT_ENTRY_LEN_SHIFT;

    // FAT cluster count from 0, 1, 2, ...
    u32 dest_sect = base_sect + ( index >> shift );
    
    u32 dest_offset = index % (1u << shift);
    //u32 dest_index = index & (( 1u << shift ) - 1u );

    // read sector
    read_block(buffer, dest_sect, 1);
    return get_u32(buffer + (dest_offset << FAT32_FAT_ENTRY_LEN_SHIFT));
}