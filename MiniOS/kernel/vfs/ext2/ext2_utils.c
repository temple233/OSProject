#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/ext2.h>
#include <zjunix/utils.h>
#include <zjunix/slab.h>
#include <zjunix/log.h>
#include <driver/vga.h>
#include <driver/ps2.h>
#include <driver/sd.h>

// 外部变量
extern struct dentry                    * root_dentry;              // vfs.c
extern struct dentry                    * pwd_dentry;
extern struct vfsmount                  * root_mnt;
extern struct vfsmount                  * pwd_mnt;

extern struct cache                     * dcache;                   // vfscache.c
extern struct cache                     * pcache;
extern struct cache                     * icache;

extern struct super_operations ext2_super_operations;                 // ext2_super_operations.c

extern struct inode_operations ext2_inode_operations[2];              // ext2_inode_operations.c

extern struct dentry_operations ext2_dentry_operations;               // ext2_dentry_operations.c

extern struct file_operations ext2_file_operations;                   // ext2_file_operations.c

extern struct address_space_operations ext2_address_space_operations; // ext2_address_space_operations.c


u32 ext2_check_inode_bitmap(struct inode *inode){
    u8 buffer[SECTOR_SIZE];
    u32 err;
    u32 sect;
    u32 state;
    u32 group_sect_base;
    u32 inodes_per_group;
    struct ext2_base_information * ext2_bi;

    // 找到inode 位图
    group_sect_base = ext2_group_base_sect(inode);
    if (group_sect_base == 0)
        return 0;

    // 读取inode位图
    ext2_bi             = (struct ext2_base_information *) inode->i_sb->s_fs_info;
    inodes_per_group    = ext2_bi->sb.attr->inodes_per_group;
    sect                = group_sect_base + 1 * (inode->i_blksize >> SECTOR_SHIFT) + \
                            ( (inode->i_ino - 1) % inodes_per_group) / SECTOR_SIZE / BITS_PER_BYTE;

    err = read_block(buffer, sect, 1);
    if (err)
        return 0;

    state = get_bit(buffer, (inode->i_ino - 1 ) % inodes_per_group % (SECTOR_SIZE * BITS_PER_BYTE));
    return state;
}



// 找到inode所在组的基地址
u32 ext2_group_base_sect(struct inode * inode){
    u8 buffer[SECTOR_SIZE];
    u32 err;
    u32 base;
    u32 sect;
    u32 group;
    u32 offset;
    u32 blksize;
    u32 group_sect_base;
    u32 group_block_base;
    u32 inodes_per_group;
    struct ext2_base_information    * ext2_bi;

    // meat data from super block
    // inode number => group ID
    // GDT lookup
    // group base address
    // in block unit
    // in sector unit
    // return this base address
    // how about inner-group offset
    // ???
    // ???
    // ???
    ext2_bi             = (struct ext2_base_information *) inode->i_sb->s_fs_info;
    base                = ext2_bi->ex_base;
    blksize             = inode->i_blksize;
    inodes_per_group    = ext2_bi->sb.attr->inodes_per_group;
    group               = (inode->i_ino - 1) / inodes_per_group;            // count from 1 inode address
    sect                = ext2_bi->ex_first_gdt_sect + group / (SECTOR_SIZE / EXT2_GROUP_DESC_BYTE);
    offset              = group % (SECTOR_SIZE / EXT2_GROUP_DESC_BYTE) * EXT2_GROUP_DESC_BYTE;

    // 读入数据，提取组描述符
    err = read_block(buffer, sect, 1);
    if (err)
        return 0;
    group_block_base    = get_u32(buffer + offset);
    group_sect_base     = base + group_block_base * (inode->i_blksize >> SECTOR_SHIFT);

    return group_sect_base;
}
