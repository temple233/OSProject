#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/fat32.h>
#include <zjunix/vfs/vfs.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/utils.h>

// 外部变量
extern struct dentry                    * root_dentry;              // vfs.c
extern struct dentry                    * pwd_dentry;
extern struct vfsmount                  * root_mnt;
extern struct vfsmount                  * pwd_mnt;

extern struct cache                     * dcache;                   // vfscache.c
extern struct cache                     * pcache;
extern struct cache                     * icache;

struct address_space_operations fat32_address_space_operations = {
    .writepage  = fat32_writepage,
    .readpage   = fat32_readpage,
    .bmap       = fat32_bmap,
};

extern struct super_operations fat32_super_operations;

extern struct inode_operations fat32_inode_operations[2];

extern struct dentry_operations fat32_dentry_operations;

extern struct file_operations fat32_file_operations;

// address_operation
// read a page(data, location, mapping(address space in inode))
u32 fat32_readpage(struct vfs_page *page)
{
    u32 err;
    u32 data_base;
    u32 abs_sect_addr;
    struct inode *inode;

    // sector ID
    // i_blksize == cluster size
    // cluster size / sector size = # sec per clu
    inode = page->p_mapping->a_inode;
    data_base = ((struct fat32_basic_information *)(inode->i_sb->s_fs_info))->fa_FAT->data_sec;
    abs_sect_addr = data_base + (page->p_location - 2) * (inode->i_blksize >> SECTOR_SHIFT);

    // read a cluster
    page->p_data = ( u8* )kmalloc(sizeof(u8) * inode->i_blksize);
    if (page->p_data == 0)
        return -ENOMEM;
    // how many sectors
    err = read_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);
    if (err)
        return -EIO;
    
    return 0;
}
// write page/cluster by inode.address space
// also by cluster ID
// == p_location
// inode->i_blksize == cluster size
// cluster size / sector size = # sec per clu
u32 fat32_writepage(struct vfs_page *page)
{
    struct inode *belonged_inode = page->p_mapping->a_inode;
    u32 data_sec = ((struct fat32_basic_information *)(belonged_inode->i_sb->s_fs_info))->fa_FAT->data_sec;
    u32 sec_id = data_sec + (page->p_location - 2) * (belonged_inode->i_blksize >> SECTOR_SHIFT);
    write_block(page->p_data, sec_id, belonged_inode->i_blksize >> SECTOR_SHIFT);

    return 0;
}

// page number of a file => cluster ID
u32 fat32_bmap(struct inode* file_inode, u32 page_number)
{
    return file_inode->i_addr.a_page[page_number];
}
