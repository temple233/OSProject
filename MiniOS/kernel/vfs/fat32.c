#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/fat32.h>
#include "../../include/zjunix/vfs/vfs.h"

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

struct vfs_page * tempp;

// VFS的接口函数
struct super_operations fat32_super_operations = {
    .delete_inode   = fat32_delete_inode,
    .write_inode    = fat32_write_inode,
};

struct inode_operations fat32_inode_operations[2] = {
    {
        .lookup = fat32_inode_lookup,
        .create = fat32_create,
    },
    {
        .create = fat32_create,
    }
};

struct dentry_operations fat32_dentry_operations = {
    .compare    = generic_compare_filename,
};

struct file_operations fat32_file_operations = {
    .read		= generic_file_read,
    .write      = generic_file_write,
    .flush      = generic_file_flush,
    .readdir    = fat32_readdir,
};

struct address_space_operations fat32_address_space_operations = {
    .writepage  = fat32_writepage,
    .readpage   = fat32_readpage,
    .bmap       = fat32_bmap,
};

// 初始化基地址为base（绝对扇区地址）上的FAT32文件系统
u32 init_fat32(u32 base)
{
    // fat32 basic information
    struct fat32_basic_information *fat32_bi = (struct fat32_basic_information *)kmalloc(sizeof(struct fat32_basic_information));
    fat32_bi->fa_DBR = (struct fat32_dos_boot_record *)kmalloc(sizeof(struct fat32_dos_boot_record));

    // initialize sector ID
    fat32_bi->fa_DBR->base = base;

    // read first sector
    u8 *dbr_data = fat32_bi->fa_DBR->data;
    read_block(dbr_data, base, 1);

    fat32_bi->fa_DBR->sec_per_clu = *(dbr_data + 0x0d);
    fat32_bi->fa_DBR->reserved = get_u16(dbr_data + 0x0e);
    fat32_bi->fa_DBR->fat_num = *(dbr_data + 0x10);
    fat32_bi->fa_DBR->fat_size = get_u32(dbr_data + 0x24);
    fat32_bi->fa_DBR->root_clu = get_u32(dbr_data + 0x2c);

    // read second sector filesystem
    fat32_bi->fa_FSINFO = (struct fat32_file_system_information *)kmalloc(sizeof(struct fat32_file_system_information));
    fat32_bi->fa_FSINFO->base = base + 1;

    u8 *fs_info_data = fat32_bi->fa_FSINFO->data;
    read_block(fs_info_data, fat32_bi->fa_FSINFO->base, 1);

    // FTA build
    fat32_bi->fa_FAT = (struct fat32_file_allocation_table *)kmalloc(sizeof(struct fat32_file_allocation_table));
    fat32_bi->fa_FAT->base = base + fat32_bi->reserved;
    // data sector begin
    fat32_bi->fa_FAT->data_sec = fat32_bi->fa_FAT->base +
                                 fat32_bi->fa_DBR->fat_num *
                                 fat32_bi->fa_DBR->fat_size;
    fat32_bi->fa_FAT->root_sec = fat32_bi->fat_FAT->data_sec +
                                 (fat32_bi->fa_DBR->root_clu  - 2) *
                                 fat_bi->fa_DBR->sec_per_clu;

    // file system type
    // for super block
    struct file_system_type *fat32_fs_type = (struct file_system_type *)kmalloc(sizeof(struct file_system_type));
    fat32_fs_type->name = "fat32";

    struct super_block *fat32_sb = (struct super_block *)kmalloc(sizeof(struct super_block));

    *fat32_sb = {
        .s_dirt = S_CLEAR,

        // 512B * N == cluster size
        .s_blksize = fat32_bi->fa_DBR->sec_per_clu << SECTOR_SHIFT,
        .s_type = fat32_fs_type,
        // dentry object *
        .s_root = NULL,
        // specific file system meta data
        .s_fs_info = (void *)fat32_bi,
        .s_op = fat32_super_operations
    };

    // root dentry object *
    // an extern global variable
    root_dentry = (struct dentry*)kmalloc(sizeof(struct dentry));
    *root_dentry = {
        .d_count = 1,
        .d_mounted = 0, // mounted point
        // parent children
        .d_inode = NULL,
        .d_parent = NULL,
        .d_name.name = "/",
        .d_name_len = 1,
        // file system
        .d_sb = fat32_sb,
        .d_op = fat32_dentry_operations
    };
    // build 4 linked-list
    INIT_LIST_HEAD(&(root_dentry->d_hash));
    INIT_LIST_HEAD(&(root_dentry->d_LRU));
    // subdir
    INIT_LIST_HEAD(&(root_dentry->d_subdirs));
    // sublings sisters
    INIT_LIST_HEAD(&(root_dentry->d_child));
    // hard link sublings sisters
    INIT_LIST_HEAD(&(root_dentry->d_alias));
    dcache->c_op->add(dcache, (void *)root_dentry);

    // change into file system
    pwd_dentry = root_dentry;
    fat32_sb->s_root = root_entry;

    // root entry inode entry, inode ID
    struct inode *root_inode = (struct inode *)kmalloc(sizeof(struct inode));
    *root_inode = {
        .i_count = 1,
        // in FAT32
        // inode ID = first cluster ID
        .i_ino = fat32_bi->fa_DBR->root_clu,
        .i_op = fat32_inode_operations[0];
        .i_fop = fat32_file_operations,
        .i_sb = fat32_sb,
        // [15] block *
        .blocks = 0,
        // == cluster size
        .i_blksize = fat32_sb->s_blksize;
    };
    // block size => 1 << block shift bit
    u32 temp_blksize = root_inode->i_blksize;
    u32 temp_blkbits = 0u;
    while(temp_blksize >>= 1u) {
        temp_blkbits++;
    }
    root_inode->i_blkbits = temp_blkbits;
    // 3 linked-list
    // all dentry referenced this inode
    INIT_LIST_HEAD(&(root_inode->i_dentry));
    // hash
    INIT_LIST_HEAD(&(root_inode->i_hash));
    // LRU
    INIT_LIST_HEAD(&(root_inode->i_LRU));

    // root dentry & root inode
    root_dentry->d_inode = root_inode;
    list_add(&(root_dentry->d_alias), &(root_inode->i_dentry));

    // build inode address space
    root_inode->i_addr = {
        .a_inode = root_inode,

        // pagesize == block size
        // 4KB 8KB
        .a_pagesize = fat32_sb->s_blksize,
        .a_op = fat32_address_space_operations
    };
    // has cahced page list
    INIT_LIST_HEAD(&(root_inode->i_addr.a_cache));

    // make sure
    // how many blocks(cluster in FAT32) for root directory
    int block_count = 0;
    u32 next_clu = far32_bi->fa_DBR->root_clu;
    while(next_clu != 0x0fffffff) {
        block_count++;
        next_clu = read_fat()
    }
}   

// 下面是为fat32专门实现的 super_operations
// 删除内存中的VFS索引节点和磁盘上文件数据及元数据
u32 fat32_delete_inode(struct dentry *dentry)

// 用通过传递参数指定的索引节点对象的内容更新一个文件系统的索引节点
u32 fat32_write_inode(struct inode * inode, struct dentry * parent)

// 下面是为fat32专门实现的 inode_operations
// 尝试在外存中查找需要的dentry对应的inode。若找到，相应的inode会被新建并加入高速缓存，dentry与之的联系也会被建立
struct dentry* fat32_inode_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)


u32 fat32_create(struct inode *dir, struct dentry *dentry, u32 mode, struct nameidata *nd)

// 下面是为fat32专门实现的file operations
u32 fat32_readdir(struct file * file, struct getdent * getdent)

// 下面是为fat32专门实现的其他方法
// 文件名双向转换
void fat32_convert_filename(struct qstr* dest, const struct qstr* src, u8 mode, u32 direction)

// 下面是为fat32专门实现的 address_space_operations
// 从外存读入一页
u32 fat32_readpage(struct vfs_page *page)

// 把一页写回外存
u32 fat32_writepage(struct vfs_page *page)

// 根据由相对文件页号得到相对物理页号
u32 fat32_bmap(struct inode* inode, u32 pageNo)

// 读文件分配表
u32 read_fat(struct inode* inode, u32 index)
{
    u8 buffer[SECTOR_SIZE];

    struct fat32_basic_information * FAT32_bi = (struct fat32_basic_information *)(inode->i_sb->s_fs_info);
    u32 base_sect = FAT32_bi->fa_FAT->base;

    // 512B / 4B for a entry
    u32 shift = SECTOR_SHIFT - FAT32_FAT_ENTRY_LEN_SHIFT;

    // FAT cluster count from 0, 1, 2, ...
    u32 dest_sect = base_sect + ( index >> shift );
    
    // 
    u8 dest_index = index & (( 1u << shift ) - 1u );

    // 读扇区并取相应的项
    read_block(buffer, dest_sect, 1);
    return get_u32(buffer + (dest_index << FAT32_FAT_ENTRY_LEN_SHIFT));
}