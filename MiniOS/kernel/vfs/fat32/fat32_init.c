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

/**
 * dummy all
 */
/**
 * dummy all
 */
static struct list_head dummy;
static struct address_space dummy_address_space;

extern struct super_operations fat32_super_operations;

extern struct inode_operations fat32_inode_operations[2];

extern struct dentry_operations fat32_dentry_operations;

extern struct file_operations fat32_file_operations;

extern struct address_space_operations fat32_address_space_operations;

/**
 * DBR read
 * file system information read
 * root inode build
 * root dentry pre read
 * root mount build
 * pwd mount build
 */
u32 init_fat32(u32 base)
{
    // fat32 basic information
    struct fat32_basic_information *fat32_bi = (struct fat32_basic_information *)kmalloc(sizeof(struct fat32_basic_information));
    fat32_bi->fa_DBR = (struct fat32_dos_boot_record *)kmalloc(sizeof(struct fat32_dos_boot_record));

    // initialize sector ID
    fat32_bi->fa_DBR->base = base;
    kernel_memset(fat32_bi->fa_DBR->data, 0, sizeof(fat32_bi->fa_DBR->data));
    
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
    kernel_memset(fat32_bi->fa_FSINFO->data, 0, sizeof(fat32_bi->fa_FSINFO->data));

    u8 *fs_info_data = fat32_bi->fa_FSINFO->data;
    read_block(fs_info_data, fat32_bi->fa_FSINFO->base, 1);

    // FTA build
    fat32_bi->fa_FAT = (struct fat32_file_allocation_table *)kmalloc(sizeof(struct fat32_file_allocation_table));
    fat32_bi->fa_FAT->base = base + fat32_bi->fa_DBR->reserved;
    // data sector begin
    fat32_bi->fa_FAT->data_sec = fat32_bi->fa_FAT->base +
                                 fat32_bi->fa_DBR->fat_num *
                                 fat32_bi->fa_DBR->fat_size;
    fat32_bi->fa_FAT->root_sec = fat32_bi->fa_FAT->data_sec +
                                 (fat32_bi->fa_DBR->root_clu  - 2) *
                                 fat32_bi->fa_DBR->sec_per_clu;

    // file system type
    // for super block
    struct file_system_type *fat32_fs_type = (struct file_system_type *)kmalloc(sizeof(struct file_system_type));
    fat32_fs_type->name = "fat32";

    struct super_block *fat32_sb = (struct super_block *)kmalloc(sizeof(struct super_block));

    *fat32_sb = (struct super_block){
        .s_dirt = S_CLEAR,

        // 512B * N == cluster size
        .s_blksize = fat32_bi->fa_DBR->sec_per_clu << SECTOR_SHIFT,
        .s_type = fat32_fs_type,
        // dentry object *
        .s_root = 0,
        // specific file system meta data
        .s_fs_info = (void *)fat32_bi,
        .s_op = &fat32_super_operations
    };

    // root dentry object *
    // an extern global variable
    root_dentry = (struct dentry*)kmalloc(sizeof(struct dentry));
    *root_dentry = (struct dentry){
        .d_count = 1,
        .d_pinned = 1,
        .d_mounted = 0, // mounted point
        
        .d_hash = dummy,
        .d_LRU = dummy,
        .d_child = dummy,
        .d_subdirs = dummy,
        .d_alias = dummy,

        // parent children
        .d_inode = 0,
        .d_parent = 0,
        .d_name.name = "/",
        .d_name.len = 1,
        .d_name.hash = 0x5a5a5a5au,
        // file system
        .d_sb = fat32_sb,
        .d_op = &fat32_dentry_operations
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
    fat32_sb->s_root = root_dentry;

    // root entry inode entry, inode ID
    struct inode *root_inode = (struct inode *)kmalloc(sizeof(struct inode));
    *root_inode = (struct inode){
        .i_count = 1,
        // in FAT32
        // inode ID = first cluster ID
        .i_ino = fat32_bi->fa_DBR->root_clu,
        .i_op = &fat32_inode_operations[0],
        .i_fop = &fat32_file_operations,
        .i_sb = fat32_sb,
        // [15] block *
        .i_blocks = 0,
        // == cluster size
        .i_blksize = fat32_sb->s_blksize,

        .i_blkbits = 0,
        .i_size = 0,
        .i_hash = dummy,
        .i_LRU = dummy,
        .i_dentry = dummy,
        .i_addr = dummy_address_space
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
    root_inode->i_addr = (struct address_space){
        .a_inode = root_inode,

        // pagesize == block size
        // 4KB 8KB
        .a_pagesize = fat32_sb->s_blksize,
        .a_op = &fat32_address_space_operations,

        .a_cache = dummy,
        // array allocate later
        .a_page = 0
    };
    // has cahced page list
    INIT_LIST_HEAD(&(root_inode->i_addr.a_cache));

    // make sure
    // how many blocks(cluster in FAT32) for root directory
    int block_count = 0;
    u32 next_clu = fat32_bi->fa_DBR->root_clu;
    while(next_clu != 0x0fffffff) {
        block_count++;
        next_clu = read_fat(root_inode, next_clu);
    }
    root_inode->i_blocks = block_count;

    // root_inode i_addr.a_page
    // file -> logical virtual page
    root_inode->i_addr.a_page = (u32 *)kmalloc(root_inode->i_blocks * sizeof(u32));

    kernel_memset(root_inode->i_addr.a_page, 0, root_inode->i_blocks);

    next_clu = fat32_bi->fa_DBR->root_clu;
    for(unsigned k = 0; k < root_inode->i_blocks; k++) {
        root_inode->i_addr.a_page[k] = next_clu;
        next_clu = read_fat(root_inode, next_clu);
    }

    // pre read root page
    for (unsigned i = 0; i < root_inode->i_blocks; i++){
        struct vfs_page *cur_page = (struct vfs_page *)kmalloc( sizeof(struct vfs_page) );

        cur_page->p_state = P_CLEAR;
        cur_page->p_location = root_inode->i_addr.a_page[i];
        cur_page->p_mapping = &(root_inode->i_addr);
        // hash
        INIT_LIST_HEAD(&(cur_page->p_hash));
        // LRU
        INIT_LIST_HEAD(&(cur_page->p_LRU));
        // p
        INIT_LIST_HEAD(&(cur_page->p_list));
        
        // p_data read
        cur_page->p_mapping->a_op->readpage(cur_page);

        // cur_page build over

        pcache->c_op->add(pcache, (void*)cur_page);

        // also add into root_inode->i_addr.a_cache
        // in a inode
        list_add(&(cur_page->p_list), &(cur_page->p_mapping->a_cache));
    }

    // flobal extern variable
    root_mnt = (struct vfsmount *)kmalloc(sizeof(struct vfsmount));
    *root_mnt = (struct vfsmount){
        // parent file system
        .mnt_parent = root_mnt,
        // point dir
        .mnt_mountpoint = root_dentry,
        // root dir
        .mnt_root = root_dentry,
        // my super block
        .mnt_sb = fat32_sb,

        .mnt_hash = dummy
    };
    // mnt_hash
    INIT_LIST_HEAD(&(root_mnt->mnt_hash));

    pwd_mnt = root_mnt;

    return 0;
}   
