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

extern struct dentry_operations ext2_dentry_operations;               // ext2_dentry_operations.c

extern struct file_operations ext2_file_operations;                   // ext2_file_operations.c

extern struct address_space_operations ext2_address_space_operations; // ext2_address_space_operations.c


struct inode_operations ext2_inode_operations[2] = {
    {
        .lookup = ext2_inode_lookup,
        .create = ext2_create,
    },
    {
        .create = ext2_create,
    }
};

struct dentry *ext2_inode_lookup(struct inode *dir, struct dentry *dentry, struct file_find_helper *ffh) {
    u8 *data;
    u8 *end;
    u32 i;
    u32 err;
    u32 addr;
    u32 found;
    u32 curPageNo;
    struct condition cond;
    struct qstr qstr;
    struct vfs_page *curPage;
    struct address_space *mapping;
    struct inode *new_inode;
    struct ext2_dir_entry *ex_dir_entry;

    found = 0;
    new_inode = 0;
    mapping = &(dir->i_addr);

    // 对目录关联的每一页
    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            return ERR_PTR(-ENOENT);

        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *) pcache->c_op->look_up(pcache, &cond);

        // 如果页高速缓存中没有，则需要在外存中寻找（一定能够找到，因为不是创建文件）
        if ( curPage == 0 ){
            curPage = (struct vfs_page *) kmalloc ( sizeof(struct vfs_page) );
            if (!curPage)
                return ERR_PTR(-ENOMEM);

            curPage->p_state    = P_CLEAR;
            curPage->p_location = curPageNo;
            curPage->p_mapping  = mapping;
            INIT_LIST_HEAD(&(curPage->p_hash));
            INIT_LIST_HEAD(&(curPage->p_LRU));
            INIT_LIST_HEAD(&(curPage->p_list));

            err = mapping->a_op->readpage(curPage);
            if ( IS_ERR_VALUE(err) ){
                release_page(curPage);
                return 0;
            }

            curPage->p_state = P_CLEAR;
            pcache->c_op->add(pcache, (void*)curPage);
            list_add(&(curPage->p_list), &(mapping->a_cache));
        }

        //现在data指向的数据就是页的数据。对每一个目录项
        data = curPage->p_data;
        end = data + dir->i_blksize;
        while ( *data != 0 && data != end ) {
            ex_dir_entry = (struct ext2_dir_entry *)data;
            qstr.len = ex_dir_entry->name_len;
            qstr.name = ex_dir_entry->name;

            if ( generic_compare_filename( &qstr, &(dentry->d_name) ) == 0 ){
                // 初步填充inode的相应信息
                new_inode = (struct inode*) kmalloc ( sizeof(struct inode) );
                new_inode->i_ino            = ex_dir_entry->ino;
                new_inode->i_blksize        = dir->i_blksize;
                new_inode->i_sb             = dir->i_sb;

                // 通过检查inode位图来检查inode是否已被删除
                if ( 0 == ext2_check_inode_bitmap(new_inode)){
                    kfree(new_inode);
                    data += (ex_dir_entry->rec_len);
                    continue;
                }

                new_inode->i_count          = 1;
                new_inode->i_blkbits        = dir->i_blkbits;
                new_inode->i_fop            = &(ext2_file_operations);
                INIT_LIST_HEAD(&(new_inode->i_dentry));

                ext2_fill_inode(new_inode);

                if ( ex_dir_entry->file_type == EXT2_FT_DIR )                      // 目录和非目录有不同的inode方法
                    new_inode->i_op         = &(ext2_inode_operations[0]);
                else
                    new_inode->i_op         = &(ext2_inode_operations[1]);

                // 填充关联的address_space结构
                new_inode->i_addr.a_inode        = new_inode;
                new_inode->i_addr.a_pagesize    = new_inode->i_blksize;
                new_inode->i_addr.a_op          = &(ext2_address_space_operations);
                INIT_LIST_HEAD(&(new_inode->i_addr.a_cache));

                // 把inode放入高速缓存
                // icache->c_op->add(icache, (void*)new_inode);
                found = 1;
                break;
            }
            data += (ex_dir_entry->rec_len);
        }
        if (found)
            break;                              // 跳出的是对每一页的循环
    }

    // 如果没找到相应的inode
    if (!found)
        return 0;

    // 完善dentry的信息
    dentry->d_inode = new_inode;
    dentry->d_op = &ext2_dentry_operations;
    list_add(&dentry->d_alias, &new_inode->i_dentry);
    
    
    return dentry;
}

u32 ext2_create(struct inode *dir, struct dentry *dentry, u32 mode, struct file_find_helper *ffh) {
    return 0;
};


// fill struct inode
// using struct ext2_inode
u32 ext2_fill_inode(struct inode *inode) {
    u8 buffer[SECTOR_SIZE];
    u32 err;
    struct ext2_inode *ex_inode;

    u32 inode_sector_id, inode_sector_offset;
    ext2_inode_find_from_ram_to_disk(inode, &inode_sector_id, &inode_sector_offset);

    err = read_block(buffer, inode_sector_id, 1);
    if (err)
        return -EIO;

    ex_inode = (struct ext2_inode *)(buffer + inode_sector_offset);
    // fill vfs information
    inode->i_blocks = ex_inode->i_blocks;
    inode->i_size = ex_inode->i_size;

    // fill address space
    inode->i_addr.a_page = (u32 *)kmalloc(EXT2_N_BLOCKS * sizeof(u32));
    if (inode->i_addr.a_page == 0)
        return -ENOMEM;
    for (u32 i = 0u; i < EXT2_N_BLOCKS; i++ )
        inode->i_addr.a_page[i] = ex_inode->i_block[i];

    return 0;
}


u32 ext2_inode_find_from_ram_to_disk(struct inode *inode, u32 *inode_sector_id, u32 *inode_sector_offset)
{
    u8 buffer[SECTOR_SIZE];
    u32 err;
    u32 base;
    u32 gd_sect;
    u32 group;
    u32 gd_offset;
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
    gd_sect                = ext2_bi->ex_first_gdt_sect + group / (SECTOR_SIZE / EXT2_GROUP_DESC_BYTE);
    gd_offset              = group % (SECTOR_SIZE / EXT2_GROUP_DESC_BYTE) * EXT2_GROUP_DESC_BYTE;

    // read group descriptor
    // dexcriptor first 4B =>
    // blockID for this group

    err = read_block(buffer, gd_sect, 1);
    if (err)
        return 0;
    group_block_base    = get_u32(buffer + gd_offset);
    group_sect_base     = base + group_block_base * (inode->i_blksize >> SECTOR_SHIFT);

    u32 inode_table_base = group_sect_base + 2 * (inode->i_blksize >> SECTOR_SHIFT);

    u32 inode_offset_in_group = (inode->i_ino - 1) % inodes_per_group;

    u32 inode_per_sector = SECTOR_SIZE / ext2_bi->sb.attr->inode_size;

    *inode_sector_id = inode_table_base + inode_offset_in_group / inode_per_sector;

    *inode_sector_offset = inode_offset_in_group % inode_per_sector;

    return group_sect_base;
}