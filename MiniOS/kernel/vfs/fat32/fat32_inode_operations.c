#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/fat32.h>
#include <zjunix/vfs/vfs.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/utils.h>

// extern
extern struct dentry                    * root_dentry;              // vfs.c
extern struct dentry                    * pwd_dentry;
extern struct vfsmount                  * root_mnt;
extern struct vfsmount                  * pwd_mnt;

extern struct cache                     * dcache;                   // vfscache.c
extern struct cache                     * pcache;
extern struct cache                     * icache;

// intern 
// private in this file
static struct list_head dummy;
static struct address_space dummy_address_space;

struct inode_operations fat32_inode_operations[2] = {
    {
        .lookup = fat32_inode_lookup,
        .create = fat32_create,
    },
    {
        .create = fat32_create,
    }
};
extern struct super_operations fat32_super_operations;

extern struct dentry_operations fat32_dentry_operations;

extern struct file_operations fat32_file_operations;

extern struct address_space_operations fat32_address_space_operations;

/**
 * dir: inode of parent dir
 * dentry: to build inode for it
 *
 * find block/cluster/page for this dentry
 * build inode
 */
struct dentry* fat32_inode_lookup(struct inode *dir, struct dentry *dentry, struct file_find_helper *ffh)
{
    u32 err;
    u32 found;

    // parent dir
    // inode -> address space
    struct address_space *p_mapping = &(dir->i_addr);

    // use for exti for loop
    struct inode *new_inode;

    for(u32 i = 0; i < dir->i_blocks; i++) {
        // get cluster ID
        u32 cur_page_number = p_mapping->a_op->bmap(dir, i);
        struct condition cond = (struct condition){
            .cond1 = (void *)(&cur_page_number),
            .cond2 = (void *)(dir)
        };
        struct vfs_page *cur_page = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);

        if(cur_page == 0) {
            cur_page = (struct vfs_page *)kmalloc(sizeof(struct vfs_page));
            *cur_page = (struct vfs_page){
                .p_state    = P_CLEAR,
                .p_location = cur_page_number,
                .p_mapping  = p_mapping
            };
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
        for ( u32 di = 0; di < dir->i_blksize; di += FAT32_DIR_ENTRY_LEN ){
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
            kernel_memset(file_name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8));
            
            struct qstr  qstr_name;
            struct qstr  qstr_name2;
            for (u32 j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                file_name[j] = dir_entry->name[j];
            qstr_name.name = file_name;
            qstr_name.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            // convert name
            fat32_convert_filename(&qstr_name2, &qstr_name, dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            // build inode for this dir entry / file
            if(generic_compare_filename(&qstr_name2, &(dentry->d_name)) == 0) {
                u32 low = dir_entry->startlo;
                u32 high = dir_entry->starthi;
                u32 clu_id = high << 16u + low;

                // build inode for this dir entry / file
                new_inode = (struct inode*)kmalloc(sizeof(struct inode));
                *new_inode = (struct inode) {
                    .i_count          = 1,
                    .i_ino            = clu_id,                 
                    .i_blkbits        = dir->i_blkbits,
                    .i_blksize        = dir->i_blksize,
                    .i_sb             = dir->i_sb,
                    .i_size           = dir_entry->size,
                    .i_blocks         = 0,
                    .i_fop            = &fat32_file_operations,

                    .i_hash = dummy,
                    .i_LRU = dummy,
                    .i_dentry = dummy,
                    .i_op = &(fat32_inode_operations[0]),
                    .i_addr = dummy_address_space
                };
                INIT_LIST_HEAD(&(new_inode->i_dentry));

                //!!!
                // dirctory and non-directory have different operations
                // just as root directory dentry [0] must be
                //!!!
                if (dir_entry->attr & ATTR_DIRECTORY)
                    new_inode->i_op = &(fat32_inode_operations[0]);
                else
                    new_inode->i_op = &(fat32_inode_operations[1]);

                new_inode->i_addr.a_inode        = new_inode;
                new_inode->i_addr.a_pagesize    = new_inode->i_blksize;
                new_inode->i_addr.a_op          = &(fat32_address_space_operations);
                INIT_LIST_HEAD(&(new_inode->i_addr.a_cache));

                            
                // make sure
                // how many blocks(cluster in FAT32) for this directory
                int block_count = 0;
                while(clu_id != 0x0fffffff) {
                    block_count++;
                    clu_id  = read_fat(new_inode, clu_id);
                }
                new_inode->i_blocks = block_count;

                // new_inode i_addr.a_page
                // file -> logical virtual page
                new_inode->i_addr.a_page = (u32 *)kmalloc(new_inode->i_blocks * sizeof(u32));

                clu_id = new_inode->i_ino;
                for(unsigned k = 0; k < new_inode->i_blocks; k++) {
                    new_inode->i_addr.a_page[k] = clu_id;
                    clu_id = read_fat(new_inode, clu_id);
                }
                
                // inode -> cache
                // icache->c_op->add(icache, (void*)new_inode);
                found = 1;
                break;
            }
        }
        if (found)
            break;          
    }

    if(!found) return 0;
    dentry->d_inode = new_inode;
    dentry->d_op = &fat32_dentry_operations;
    list_add(&(dentry->d_alias), &(new_inode->i_dentry));

    return dentry;
}

u32 fat32_create(struct inode *dir, struct dentry *dentry, u32 mode, struct file_find_helper *ffh)
{
    return 0u;
}
