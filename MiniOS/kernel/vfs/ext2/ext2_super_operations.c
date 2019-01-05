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

extern struct inode_operations ext2_inode_operations[2];              // ext2_inode_operations.c

extern struct dentry_operations ext2_dentry_operations;               // ext2_dentry_operations.c

extern struct file_operations ext2_file_operations;                   // ext2_file_operations.c

extern struct address_space_operations ext2_address_space_operations; // ext2_address_space_operations.c

struct super_operations ext2_super_operations = {
    .delete_inode = ext2_delete_inode,
    .write_inode = ext2_write_inode,
};

u32 ext2_delete_inode(struct dentry *dentry){
    u8 *data;
    u8 *start;
    u8 *end;
    u8 *copy_end;
    u8 *copy_start;
    u8 *paste_start;
    u8 *paste_end;
    u8 buffer[SECTOR_SIZE];
    u32 i;
    u32 len;
    u32 err;
    u32 flag;
    u32 sect;
    u32 found;
    u32 block;
    u32 group;
    u32 rec_len;
    u32 curPageNo;
    u32 group_base;       
    u32 blocks_per_group;
    u32 inodes_per_group;
    u32 sectors_per_block;
    u32 block_bitmap_base;              // block bitmap 所属于的组和inode所属于的不一定一样！！
    u32 inode_bitmap_base;
    u32 inode_table_base;
    struct inode                    *dir;
    struct inode                    *inode;
    struct inode                    *dummy;
    struct qstr                     qstr;
    struct condition                cond;
    struct address_space            *mapping;
    struct vfs_page                 *curPage;
    struct ext2_dir_entry           *ex_dir_entry;
    struct ext2_base_information    *ext2_bi;

    inode = dentry->d_inode;

    // 找到inode所在组的基地址(绝对扇区)，及下面要用到的基地址
    group_base                          = ext2_group_base_sect(inode);
    sectors_per_block                   = inode->i_blksize >> SECTOR_SHIFT;
    inode_bitmap_base                   = group_base + sectors_per_block;
    inode_table_base                    = group_base + (sectors_per_block << 1);

    // 清除块位图上所有相关的位
    ext2_bi             = (struct ext2_base_information *) inode->i_sb->s_fs_info;
    inodes_per_group    = ext2_bi->sb.attr->inodes_per_group;
    blocks_per_group    = ext2_bi->sb.attr->blocks_per_group;

    dummy = (struct inode*) kmalloc(sizeof(struct inode));          // 构造一个虚inode以获得block_bitmap_base
    dummy->i_blksize    = inode->i_blksize;
    dummy->i_sb         = inode->i_sb;

    for ( i = 0; i < inode->i_blocks; i++ ){
        block = ext2_bmap(inode, i);
        if  (block == 0)
            continue;

        group = block / blocks_per_group;
        dummy->i_ino = group * inodes_per_group + 1;
        block_bitmap_base = ext2_group_base_sect(dummy);

        sect = block_bitmap_base + ( block % blocks_per_group) / SECTOR_SIZE / BITS_PER_BYTE;

        err = read_block(buffer, sect, 1);
        if (err)
            return -EIO;

        reset_bit(buffer, (block % blocks_per_group) % (SECTOR_SIZE * BITS_PER_BYTE));

        err = write_block(buffer, sect, 1);
        if (err)
            return -EIO;

        err = read_block(buffer, sect, 1);
        if (err)
            return -EIO;

    }

    // 清除inode位图上相关的位，记住 没有 零号inode的bitmap
    sect = inode_bitmap_base + ((inode->i_ino - 1) % inodes_per_group) / SECTOR_SIZE / BITS_PER_BYTE;
    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;

    reset_bit(buffer, ((inode->i_ino - 1) % inodes_per_group) % (SECTOR_SIZE * BITS_PER_BYTE));

    err = write_block(buffer, sect, 1);
    if (err)
        return -EIO;

    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;
    

    // 清除inode表上的数据，记住 没有 零号inode的table item
    sect = inode_table_base + (inode->i_ino - 1) % inodes_per_group / ( SECTOR_SIZE / ext2_bi->sb.attr->inode_size);

    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;
    
    kernel_memset(buffer + ((inode->i_ino - 1) % inodes_per_group % ( SECTOR_SIZE / ext2_bi->sb.attr->inode_size)) * ext2_bi->sb.attr->inode_size, \
                    0, ext2_bi->sb.attr->inode_size);
    
    err = write_block(buffer, sect, 1);
    if (err)
        return -EIO;

    // 修改sb, 并使所有sb和gdt一致略

    // 清除父目录数据块中相应的目录项，并把后面的目录项向前移动
    flag = 0;
    found = 0;
    rec_len = 0;
    copy_start = 0;
    dir = dentry->d_parent->d_inode;
    mapping = &(dir->i_addr);
    for ( i = 0; i < dir->i_blocks; i++){      // 对父目录关联的每一页
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            return -ENOENT;

        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = pcache->c_op->look_up(pcache, &cond);

        // 如果页高速缓存中没有，则需要在外存中寻找（一定能够找到，因为不是创建文件）
        if ( curPage == 0 ){
            curPage = (struct vfs_page *) kmalloc ( sizeof(struct vfs_page) );
            if (!curPage)
                return -ENOMEM;

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
        start = data;
        end = data + inode->i_blksize;
        while ( *data != 0 && data != end) {
            ex_dir_entry = (struct ext2_dir_entry *)data;

            if (found){                                     // 确定需要前移的目录项组的始末位置
                if (flag == 0) {          
                    copy_start = data;
                    flag = 1;
                }
                copy_end = data + ex_dir_entry->rec_len;
            }
            else {
                qstr.len = ex_dir_entry->name_len;
                qstr.name = ex_dir_entry->name;
                if ( generic_compare_filename( &qstr, &(dentry->d_name) ) == 0 ){  // 如果找到相应的目录项
                    paste_start = data;
                    found = 1;
                    rec_len = ex_dir_entry->rec_len;
                }
            }  
            data += (ex_dir_entry->rec_len);
        }

        if (found)
            break;                              // 跳出的是对每一页的循环
    }

    // 如果没找到相应的inode
    if (!found)
        return -ENOENT;

    // 抹掉原来目录项的信息
    for ( i = 0; i < rec_len; i++ )
        *(paste_start+i) = 0;

    // 如果被删除的目录项后面有目录项，需要前移
    if (copy_start != 0) {
        len = (u32)copy_end - (u32)copy_start;              // 前移
        kernel_memcpy(paste_start, copy_start, len);

        paste_end = paste_start + len;
        len = (u32)copy_end - (u32)paste_end;             // 清理尾巴
        for ( i = 0; i < len; i++ )
            *(paste_end+i) = 0;

        err = ext2_writepage(curPage);                      // 写回内存
        if (err)
            return err;
    }

    return 0;
}

// 用通过传递参数指定的索引节点对象的内容更新一个文件系统的索引节点
u32 ext2_write_inode(struct inode * inode, struct dentry * parent){
    u8 buffer[SECTOR_SIZE];
    u32 i;
    u32 err;
    u32 sect;
    u32 group_sect_base;
    u32 inodes_per_group;
    struct ext2_inode               * ex_inode;
    struct ext2_base_information    * ext2_bi;

    // 首先的得到对应组的基地址（块位图所在的块）
    group_sect_base = ext2_group_base_sect(inode);
    if (group_sect_base == 0)
        return -EIO;

    // 接下来找到inode数据（inode表内）所在的绝对扇区，并读入相应的数据
    ext2_bi             = (struct ext2_base_information *) inode->i_sb->s_fs_info;
    inodes_per_group    = ext2_bi->sb.attr->inodes_per_group;
    sect                = group_sect_base + 2 * (inode->i_blksize >> SECTOR_SHIFT) + \
                            ( (inode->i_ino - 1) % inodes_per_group ) / ( SECTOR_SIZE / ext2_bi->sb.attr->inode_size);
    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;
    
    ex_inode            = (struct ext2_inode *)(buffer + \
        (inode->i_ino - 1) % ( SECTOR_SIZE / ext2_bi->sb.attr->inode_size) * ext2_bi->sb.attr->inode_size );

    // 然后修改buffer中VFS的inode信息
    ex_inode->i_size                   = inode->i_size;

    // 把修改写入外存
    err = write_block(buffer, sect, 1);
    if (err)
        return -EIO;

    return 0;
}
