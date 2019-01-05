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

// initlize EXT2 flie system
// base MBR get it
// or EBR get it
u32 init_ext2(u32 base){
    u32 i;
    u32 err;
    u32 p_location;
    struct vfs_page                     * curPage;
    struct ext2_base_information        * ext2_bi;
    struct file_system_type             * ext2_fs_type;
    struct super_block                  * ext2_sb;
    struct dentry                       * ext2_root_dentry;
    struct inode                        * ext2_root_inode;
    struct vfsmount                     * ext2_root_mnt;

    // ext2_basic_information
    ext2_bi = (struct ext2_base_information *)kmalloc(sizeof(struct ext2_base_information));
    if (ext2_bi == 0)
        return -ENOMEM;
    ext2_bi->ex_base = base;
    ext2_bi->ex_first_sb_sect = ext2_bi->ex_base + EXT2_BOOT_BLOCK_SECT;
    
    // ext2 super block
    // which is very same with VFS
    ext2_bi->sb.data = (u8 *)kmalloc(sizeof(u8) * EXT2_SUPER_BLOCK_SECT * SECTOR_SIZE);
    if (ext2_bi->sb.data == 0)
        return -ENOMEM;
    err = read_block(ext2_bi->sb.data, ext2_bi->ex_first_sb_sect, EXT2_SUPER_BLOCK_SECT);
    if (err)
        return -EIO;

    // find GBT in group0
    ext2_bi->ex_first_gdt_sect = base + (( EXT2_BASE_BLOCK_SIZE << ext2_bi->sb.attr->block_size) >> SECTOR_SHIFT);
    
    // build file system type information
    ext2_fs_type = (struct file_system_type *) kmalloc ( sizeof(struct file_system_type) );
    if (ext2_fs_type == 0)
        return -ENOMEM;
    ext2_fs_type->name = "ext2";

    // super_block for this file system
    ext2_sb = (struct super_block *) kmalloc ( sizeof(struct super_block) );
    if (ext2_sb == 0)
        return -ENOMEM;
    ext2_sb->s_dirt    = S_CLEAR;
    ext2_sb->s_blksize = EXT2_BASE_BLOCK_SIZE << ext2_bi->sb.attr->block_size;
    ext2_sb->s_type    = ext2_fs_type;
    ext2_sb->s_root    = 0;
    ext2_sb->s_fs_info = (void*)ext2_bi;
    ext2_sb->s_op      = &ext2_super_operations;

    // build root dentry for this filesystem
    // ext2_sb->s_root = 
    // the following
    ext2_root_dentry = (struct dentry *) kmalloc ( sizeof(struct dentry) );
    if (ext2_root_dentry == 0)
        return -ENOMEM;
    ext2_root_dentry->d_count           = 1;
    ext2_root_dentry->d_mounted         = 0;
    ext2_root_dentry->d_inode           = 0;
    ext2_root_dentry->d_parent          = 0;
    ext2_root_dentry->d_name.name       = "/";
    ext2_root_dentry->d_name.len        = 1;
    ext2_root_dentry->d_sb              = ext2_sb;
    ext2_root_dentry->d_op              = &ext2_dentry_operations;
    INIT_LIST_HEAD(&(ext2_root_dentry->d_hash));
    INIT_LIST_HEAD(&(ext2_root_dentry->d_LRU));
    INIT_LIST_HEAD(&(ext2_root_dentry->d_subdirs));
    INIT_LIST_HEAD(&(ext2_root_dentry->d_child));
    INIT_LIST_HEAD(&(ext2_root_dentry->d_alias));
    dcache->c_op->add(dcache, (void*)ext2_root_dentry);

    // add it to super block
    ext2_sb->s_root = ext2_root_dentry;


    // build root inode for this dentry
    // ext2_root_dentry->d_inode = 
    // the following
    ext2_root_inode = (struct inode *) kmalloc ( sizeof(struct inode) );
    if (ext2_root_inode == 0)
        return -ENOMEM;
    ext2_root_inode->i_count            = 1;
    ext2_root_inode->i_ino              = EXT2_ROOT_INO;
    ext2_root_inode->i_op               = &(ext2_inode_operations[0]);
    ext2_root_inode->i_fop              = &ext2_file_operations;
    ext2_root_inode->i_sb               = ext2_sb;
    ext2_root_inode->i_blksize          = ext2_sb->s_blksize;
    INIT_LIST_HEAD(&(ext2_root_inode->i_hash));
    INIT_LIST_HEAD(&(ext2_root_inode->i_LRU));
    INIT_LIST_HEAD(&(ext2_root_inode->i_dentry));

    if(ext2_root_inode->i_blksize == 1024) {
        ext2_root_inode->i_blkbits = 10;
    } else if(ext2_root_inode->i_blksize == 2048) {
        ext2_root_inode->i_blkbits = 11;
    } else if(ext2_root_inode->i_blksize == 4096) {
        ext2_root_inode->i_blkbits = 12;
    } else if(ext2_root_inode->i_blksize == 8192) {
        ext2_root_inode->i_blkbits = 13;
    }

    // build address for this inode
    // ext2_root_inode->i_addr = 
    // the following
    ext2_root_inode->i_addr.a_inode      = ext2_root_inode;
    ext2_root_inode->i_addr.a_pagesize  = ext2_sb->s_blksize;
    ext2_root_inode->i_addr.a_op        = &(ext2_address_space_operations);
    INIT_LIST_HEAD(&(ext2_root_inode->i_addr.a_cache));

    // fill by disk inode in ext2
    err = ext2_fill_inode(ext2_root_inode);
    if (err)
        return err;

    // pre read root directory entry
    for (i = 0; i < ext2_root_inode->i_blocks; i++){
        
        p_location = ext2_root_inode->i_addr.a_op->bmap(ext2_root_inode, i);
        if (p_location == 0)
            continue;

        curPage = (struct vfs_page *) kmalloc(sizeof(struct vfs_page));
        if (curPage == 0)
            return -ENOMEM;

        curPage->p_state = P_CLEAR;
        curPage->p_location = p_location;
        curPage->p_mapping = &(ext2_root_inode->i_addr);
        INIT_LIST_HEAD(&(curPage->p_hash));
        INIT_LIST_HEAD(&(curPage->p_LRU));
        INIT_LIST_HEAD(&(curPage->p_list));

        err = curPage->p_mapping->a_op->readpage(curPage);
        if ( IS_ERR_VALUE(err) ) {
            release_page(curPage);
            return err;
        }
        
        pcache->c_op->add(pcache, (void*)curPage);
        list_add(&(curPage->p_list), &(curPage->p_mapping->a_cache));
    }

    // 与根目录的dentry关联
    ext2_root_dentry->d_inode = ext2_root_inode;
    list_add(&(ext2_root_dentry->d_alias), &(ext2_root_inode->i_dentry));

    // 构建本文件系统关联的 vfsmount 结构
    ext2_root_mnt = (struct vfsmount *) kmalloc ( sizeof(struct vfsmount));
    if (ext2_root_mnt == 0)
        return -ENOMEM;
    ext2_root_mnt->mnt_parent        = ext2_root_mnt;
    ext2_root_mnt->mnt_mountpoint    = ext2_root_dentry;
    ext2_root_mnt->mnt_root          = ext2_root_dentry;
    ext2_root_mnt->mnt_sb            = ext2_sb;
    INIT_LIST_HEAD(&(ext2_root_mnt->mnt_hash));

    // 加入mnt列表
    list_add(&(ext2_root_mnt->mnt_hash), &(root_mnt->mnt_hash));

    return 0;
}
