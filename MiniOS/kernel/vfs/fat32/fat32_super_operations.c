#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/fat32.h>
#include <zjunix/vfs/vfs.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/utils.h>

// 外部变量
extern struct dentry *root_dentry;              // vfs.c
extern struct dentry *pwd_dentry;
extern struct vfsmount *root_mnt;
extern struct vfsmount *pwd_mnt;

extern struct cache *dcache;                   // vfscache.c
extern struct cache *pcache;
extern struct cache *icache;

struct super_operations fat32_super_operations = {
    .delete_inode   = fat32_delete_inode,
    .write_inode    = fat32_write_inode,
};

extern struct inode_operations fat32_inode_operations[2];

extern struct dentry_operations fat32_dentry_operations;

extern struct file_operations fat32_file_operations;

extern struct address_space_operations fat32_address_space_operations;

/**
 * read its parent all dentries into RAM
 * compare and delete
 * write back block/cluster/page contanined this dentry
 * now it has been set 0xe5
 * no changed FAT table
 */
u32 fat32_delete_inode(struct dentry *dentry)
{
    u32 is_found;
    // remove from parent directory
    struct inode *parent_inode = dentry->d_parent->d_inode;
    struct address_space *p_mapping = &(parent_inode->i_addr);
    for(unsigned i = 0; i < parent_inode->i_blocks; i++) {
        u32 page_number = p_mapping->a_op->bmap(parent_inode, i);
        struct condition cond;
        cond.cond1 = (void *)(&page_number);
        cond.cond2 = (void *)(parent_inode);
        struct vfs_page *cur_page = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);
        if(cur_page == 0) {
            cur_page = (struct vfs_page *)kmalloc(sizeof(struct vfs_page));

            cur_page->p_state    = PG_CACHE_CLEAR;
            cur_page->p_location = page_number;
            cur_page->p_mapping  = p_mapping;
            INIT_LIST_HEAD(&(cur_page->p_hash));
            INIT_LIST_HEAD(&(cur_page->p_LRU));
            INIT_LIST_HEAD(&(cur_page->p_list));

            p_mapping->a_op->readpage(cur_page);
            pcache->c_op->add(pcache, (void *)cur_page);

            // add this page into inode cached list
            list_add(&(cur_page->p_list), &(p_mapping->a_cache));
        }

        // parse every page
        // FAT32 every cluster
        for ( u32 di = 0; di < parent_inode->i_blksize; di += FAT32_DIR_ENTRY_LEN ){
            struct fat_dir_entry *dir_entry = (struct fat_dir_entry *)(cur_page->p_data + di);

            /**
             * 00000000 read/write
             * 00000001 read
             * 00000010 hide
             * 00000100 system
             * 00001000 volumn
             * 00010000 sub dir
             * 00100000 archive
             */
            if (dir_entry->attr == 0x08 || dir_entry->attr == 0x0f ||
                dir_entry->name[0] == 0xe5)
                continue;

            // no other dir entry
            if (dir_entry->name[0] == '\000')
                break;
            
            // parse name
            u8 file_name[MAX_FAT32_SHORT_FILE_NAME_LEN];
            struct qstr  qstr_name;
            struct qstr  qstr_name2;
            
            kernel_memset(file_name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8) );

            for (u32 j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                file_name[j] = dir_entry->name[j];
            qstr_name.name = file_name;
            qstr_name.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            // convert name
            fat32_convert_filename(&qstr_name2, &qstr_name, dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            // delete dentry in parent dentry
            if(generic_compare_filename(&qstr_name2, &(dentry->d_name)) == 0) {
                dir_entry->name[0] = 0xe5;
                is_found = 1;

                // write through
                // make it consisent
                fat32_writepage(cur_page);
                break;                          
            }
        }
        if (is_found)
            break;                             
    }

    return 0;
}

/**
 * read parent all dentries into RAM
 * get first dentry related ti this inode
 * update this dentry -> file size
 * write back block/cluster/page
 * no change FAT tabel
 */
u32 fat32_write_inode(struct inode *inode, struct dentry *parent)
{
    u8 name[MAX_FAT32_SHORT_FILE_NAME_LEN];
    u32 i;
    u32 j;
    u32 err;
    u32 found;
    u32 begin;
    u32 curPageNo;
    u32 pagesize;
    struct qstr                             qstr;
    struct qstr                             qstr2;
    struct inode                            * dir;
    struct dentry                           * dentry;
    struct vfs_page                         * curPage;
    struct condition                        cond;
    struct address_space                    * mapping;
    struct fat_dir_entry                    * fat_dir_entry;

    found = 0;
    dir         = parent->d_inode;
    mapping     = &(dir->i_addr);
    pagesize    = dir->i_blksize;
    
    struct dentry *dentry_of_this_inode = container_of(inode->i_dentry.next, struct dentry, d_alias);

    // every page of this parent dentry
    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);

        if (curPage == 0){
            curPage = (struct vfs_page *)kmalloc(sizeof(struct vfs_page));
            if (!curPage)
                return -ENOMEM;

            curPage->p_state    = PG_CACHE_CLEAR;
            curPage->p_location = curPageNo;
            curPage->p_mapping  = mapping;
            INIT_LIST_HEAD(&(curPage->p_hash));
            INIT_LIST_HEAD(&(curPage->p_LRU));
            INIT_LIST_HEAD(&(curPage->p_list));

            err = mapping->a_op->readpage(curPage);
            if ( IS_ERR_VALUE(err) ){
                release_page(curPage);
                return -ENOENT;
            }

            curPage->p_state = PG_CACHE_CLEAR;
            pcache->c_op->add(pcache, (void*)curPage);
            list_add(&(curPage->p_list), &(mapping->a_cache));
        }

        // parse every page
        // FAT32 every cluster
        for ( begin = 0; begin < pagesize; begin += FAT32_DIR_ENTRY_LEN ){
            fat_dir_entry = (struct fat_dir_entry *)(curPage->p_data + begin);

            /**
             * 00000000 read/write
             * 00000001 read
             * 00000010 hide
             * 00000100 system
             * 00001000 volumn
             * 00010000 sub dir
             * 00100000 archive
             */
            if (fat_dir_entry->attr == 0x08 || fat_dir_entry->attr == 0x0f ||
                fat_dir_entry->name[0] == 0xe5)
                continue;

            // has no other dentry
            if (fat_dir_entry->name[0] == '\000')
                break;
            
            kernel_memset( name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8) );
            for ( j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                name[j] = fat_dir_entry->name[j];

            qstr.name = name;
            qstr.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            fat32_convert_filename(&qstr2, &qstr, fat_dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            if (generic_compare_filename(&qstr2, &(dentry_of_this_inode->d_name)) == 0){
                // change file size
                fat_dir_entry->size  = inode->i_size;
                found = 1;
                break;
            }
        }
        if (found) break;
    }

    if (!found)
        return -ENOENT;

    // write back disk
    err = mapping->a_op->writepage(curPage);
    if(err)
        return err;
    
    return 0;
}