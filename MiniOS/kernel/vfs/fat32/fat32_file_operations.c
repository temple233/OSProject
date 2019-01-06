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

struct file_operations fat32_file_operations = {
    .read		= generic_file_read,
    .write      = generic_file_write,
    .flush      = generic_file_flush,
    .readdir    = fat32_readdir,
};
extern struct super_operations fat32_super_operations;

extern struct inode_operations fat32_inode_operations[2];

extern struct dentry_operations fat32_dentry_operations;

extern struct address_space_operations fat32_address_space_operations;

/**
 * file: open file(inode.address_space, location/clu_id)
 * it must be dir
 * 
 * getdent (dentry[] size)
 */
u32 fat32_readdir(struct file *file, struct getdent *getdent)
{
    u8 name[MAX_FAT32_SHORT_FILE_NAME_LEN];
    u32 i;
    u32 j;
    u32 err;
    u32 addr;
    u32 low;
    u32 high;
    u32 begin;
    u32 pagesize;
    u32 curPageNo;
    struct inode                    *dir;
    struct qstr                     qstr;
    struct qstr                     qstr2;
    struct condition                cond;
    struct fat_dir_entry            *fat_dir_entry;
    struct vfs_page                 *curPage;
    struct address_space            *mapping;

    dir = file->f_dentry->d_inode;
    mapping = &(dir->i_addr);
    pagesize = dir->i_blksize;

    getdent->count = 0;
    getdent->dirent = (struct dirent *) kmalloc ( sizeof(struct dirent) * (dir->i_blocks * pagesize / FAT32_DIR_ENTRY_LEN));
    if (getdent->dirent == 0)
        return -ENOMEM;

    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            return -ENOENT;

        // cache hit
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);
        
        // read in sector
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

        // for each dentry      
        for ( begin = 0; begin < pagesize; begin += FAT32_DIR_ENTRY_LEN ) {
            fat_dir_entry = (struct fat_dir_entry *)(curPage->p_data + begin);

            if (fat_dir_entry->attr == 0x08 || fat_dir_entry->attr == 0x0f ||
                fat_dir_entry->name[0] == 0xe5)
                continue;

            if (fat_dir_entry->name[0] == '\000')
                break;
            
            // get name
            kernel_memset(name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8));
            for ( j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                name[j] = fat_dir_entry->name[j];
            
            qstr.name = name;
            qstr.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            // name
            fat32_convert_filename(&qstr2, &qstr, fat_dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            low     = fat_dir_entry->startlo;
            high    = fat_dir_entry->starthi;
            addr    = (high << 16) + low;

            getdent->dirent[getdent->count].ino         = addr;
            getdent->dirent[getdent->count].name        = qstr2.name;

            if ( fat_dir_entry->attr & ATTR_DIRECTORY )
                getdent->dirent[getdent->count].type    = FT_DIR;
            else
                getdent->dirent[getdent->count].type    = FT_REG_FILE;

            getdent->count += 1;
        } 
    }

    return 0;
}

