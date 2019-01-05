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

extern struct address_space_operations ext2_address_space_operations; // ext2_address_space_operations.c

struct file_operations ext2_file_operations = {
    .read       = generic_file_read,
    .write      = generic_file_write,
    .flush      = generic_file_flush,
    .readdir    = ext2_readdir,
};
///:~ implementation in utils.c

u32 ext2_readdir(struct file * file, struct getdent * getdent){
    u8 *data;
    u8 *name;
    u8 *end;
    u32 i;
    u32 j;
    u32 err;
    u32 pagesize;
    u32 curPageNo;
    struct inode                    *dir;
    struct inode                    *new_inode;
    struct qstr                     qstr;
    struct condition                cond;
    struct vfs_page                 *curPage;
    struct address_space            *mapping;
    struct ext2_dir_entry           *ex_dir_entry;

    dir = file->f_dentry->d_inode;
    mapping = &(dir->i_addr);
    pagesize = dir->i_blksize;
    getdent->count = 0;
    getdent->dirent = (struct dirent *) kmalloc ( sizeof(struct dirent) * (MAX_DIRENT_NUM));
    if (getdent->dirent == 0)
        return -ENOMEM;

    // 对目录关联的每一页
    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            continue;

        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *) pcache->c_op->look_up(pcache, &cond);

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
        end = data + dir->i_blksize;
        while ( *data != 0 && data != end ) {
            ex_dir_entry = (struct ext2_dir_entry *)data;
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

            qstr.len    = ex_dir_entry->name_len;
            qstr.name   = ex_dir_entry->name;

            name = 0;
            name = (u8 *) kmalloc ( sizeof(u8) * ( ex_dir_entry->name_len + 1 ));
            if (name == 0)
                return -ENOMEM;
            for ( j = 0; j < ex_dir_entry->name_len; j++)
                name[j] = qstr.name[j];
            name[j] = 0;

            getdent->dirent[getdent->count].ino     = ex_dir_entry->ino;
            getdent->dirent[getdent->count].name    = name;
            getdent->dirent[getdent->count].type    = ex_dir_entry->file_type;
            getdent->count += 1;
            
            data += (ex_dir_entry->rec_len);
        }   // 页内循环
    }   // 页际循环

    return 0;
}
