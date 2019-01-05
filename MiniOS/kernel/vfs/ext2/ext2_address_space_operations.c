#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/ext2.h>
#include <zjunix/utils.h>
#include <zjunix/slab.h>
#include <zjunix/log.h>
#include <driver/vga.h>
#include <driver/ps2.h>
#include <driver/sd.h>

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

struct address_space_operations ext2_address_space_operations = {
    .writepage  = ext2_writepage,
    .readpage   = ext2_readpage,
    .bmap       = ext2_bmap,
};


// address_space_operations
// read page
u32 ext2_readpage(struct vfs_page * page) {
    u32 err;
    u32 base;
    u32 abs_sect_addr;
    struct inode *inode;

    // 计算绝对扇区地址
    inode = page->p_mapping->a_inode;
    base = ((struct ext2_base_information *)(inode->i_sb->s_fs_info))->ex_base;
    abs_sect_addr = base + page->p_location * (inode->i_blksize >> SECTOR_SHIFT);

    // 分配数据区,调用底层函数读入数据
    page->p_data = ( u8* ) kmalloc ( sizeof(u8) * inode->i_blksize );
    if (page->p_data == 0)
        return -ENOMEM;

    // 从外存读入
    kernel_memset(page->p_data, 0, sizeof(u8) * inode->i_blksize);
    err = read_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);
    if (err)
        return -EIO;

    return 0;
}

// 把一页写回外存
u32 ext2_writepage(struct vfs_page * page) {
    u32 err;
    u32 base;
    u32 abs_sect_addr;
    struct inode *inode;

    // 计算绝对扇区地址
    inode = page->p_mapping->a_inode;
    base = ((struct ext2_base_information *)(inode->i_sb->s_fs_info))->ex_base;
    abs_sect_addr = base + page->p_location * (inode->i_blksize >> SECTOR_SHIFT);
    
    // 写到外存
    err = write_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);
    if (err)
        return -EIO;
    
    return 0;
}

// address operation
// from [0 ... blocks] in inode
// to block ID
u32 ext2_bmap(struct inode *inode, u32 curPageNo) {
    u8* data;
    u32 i;
    u32 addr;
    u32 *page;
    u32 ret_val;
    u32 first_no;
    u32 entry_num;
    page = inode->i_addr.a_page;
  
    // direct map  
    if ( curPageNo < EXT2_FIRST_MAP_INDEX ) 
        ret_val = page[curPageNo];  
    
    // u32 
    // every block ID 4B
    entry_num = inode->i_blksize >> EXT2_BLOCK_ADDR_SHIFT;
    data = (u8 *) kmalloc ( inode->i_blksize * sizeof(u8) );
    if (data == 0)
        return 0;

    // 1 index 
    curPageNo -= EXT2_FIRST_MAP_INDEX;
    if ( curPageNo < entry_num ) {  
        read_block(data, page[EXT2_FIRST_MAP_INDEX], inode->i_blksize >> SECTOR_SHIFT);
        ret_val = get_u32(data + (curPageNo << EXT2_BLOCK_ADDR_SHIFT));
        goto ok;
    }  
  
    // 2 index   
    curPageNo -= entry_num;  
    if ( curPageNo < entry_num * entry_num ){
        read_block(data, page[EXT2_SECOND_MAP_INDEX], inode->i_blksize >> SECTOR_SHIFT);
        addr = get_u32(data + ((curPageNo / entry_num) << EXT2_BLOCK_ADDR_SHIFT) );
        read_block(data, addr, inode->i_blksize >> SECTOR_SHIFT);
        ret_val = get_u32(data + ((curPageNo % entry_num) << EXT2_BLOCK_ADDR_SHIFT) );
        goto ok;
    }

    // 3 index 
    curPageNo -= entry_num * entry_num; 
    if ( curPageNo < entry_num * entry_num * entry_num ){
        read_block(data, page[EXT2_THIRD_MAP_INDEX], inode->i_blksize >> SECTOR_SHIFT);
        addr = get_u32(data + ((curPageNo / entry_num / entry_num) << EXT2_BLOCK_ADDR_SHIFT) );
        read_block(data, addr, inode->i_blksize >> SECTOR_SHIFT);
        addr = get_u32(data + ((curPageNo % (entry_num / entry_num)) << EXT2_BLOCK_ADDR_SHIFT) );
        read_block(data, addr, inode->i_blksize >> SECTOR_SHIFT);
        ret_val = get_u32(data + ((curPageNo % entry_num % entry_num) << EXT2_BLOCK_ADDR_SHIFT) );
        goto ok;
    }

ok:
    kfree(data);
    return ret_val;
};
