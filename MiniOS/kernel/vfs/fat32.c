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
struct list_head dummy;
struct address_space dummy_address_space;

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

            cur_page->p_state    = P_CLEAR;
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

            curPage->p_state    = P_CLEAR;
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

            curPage->p_state = P_CLEAR;
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

    // 如果没找到相应的inode
    if (!found)
        return -ENOENT;

    // 写入外存
    err = mapping->a_op->writepage(curPage);
    if(err)
        return err;
    
    return 0;
}
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
                break;                          // 跳出的是对每一个目录项的循环
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


// dest
// src
// mode == system reserved byte in an entry
// FAT32_NAME_NORMAL_TO_SPECIFIC 12[.] -> 11[]
// FAT32_NAME_SPECIFIC_TO_NORMAL 11[] -> 12[.]
void fat32_convert_filename(struct qstr* dest, const struct qstr* src, u8 mode, u32 direction)
{
    u8* name;
    int i;
    u32 j;
    u32 dot;
    int end;
    u32 null;
    int dot_pos;

    dest->name = 0;
    dest->len = 0;

    // 12[.] -> 11[]
    if ( direction == FAT32_NAME_NORMAL_TO_SPECIFIC ){
        u8 *name = (u8 *)kmalloc(MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8));

        // find “.”
        dot = 0;
        dot_pos = INF;
        for ( i = 0; i < src->len; i++ )
            if ( src->name[i] == '.' ){
                dot = 1;
                break;
            }
                
        if (dot)
            dot_pos = i;

        // before “.”
        if ( dot_pos > MAX_FAT32_SHORT_FILE_NAME_BASE_LEN )
            end = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN - 1;
        else
            end = dot_pos - 1;

        for ( i = 0; i < MAX_FAT32_SHORT_FILE_NAME_BASE_LEN; i++ ){
            if ( i > end )
                name[i] = '\0';
            else {
                if ( src->name[i] <= 'z' && src->name[i] >= 'a' )
                    name[i] = src->name[i] - 'a' + 'A';
                else
                    name[i] = src->name[i];
            }
        }

        // after "."
        for ( i = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN, j = dot_pos + 1; i < MAX_FAT32_SHORT_FILE_NAME_LEN; i++, j++ )
        {
            if ( j >= src->len )
                name[i] == '\0';
            else{
                if ( src->name[j] <= 'z' && src->name[j] >= 'a' )
                    name[i] = src->name[j] - 'a' + 'A';
                else
                    name[i] = src->name[j];
            }
        }
        
        dest->name = name;
        dest->len = MAX_FAT32_SHORT_FILE_NAME_LEN;
    }

    // 11[] -> 12[.]
    else if ( direction == FAT32_NAME_SPECIFIC_TO_NORMAL ) {
        null = 0;
        dot_pos = MAX_FAT32_SHORT_FILE_NAME_LEN;
        for ( i = MAX_FAT32_SHORT_FILE_NAME_LEN - 1; i  ; i-- ){
            if ( src->name[i] == 0x20 ) {
                dot_pos = i;
                null ++;
            }

        }

        dest->len = MAX_FAT32_SHORT_FILE_NAME_LEN - null;
        name = (u8 *)kmalloc((dest->len + 2) * sizeof(u8));     // 空字符 + '.'(不一定有)
        
        if ( dot_pos > MAX_FAT32_SHORT_FILE_NAME_BASE_LEN )
            dot_pos = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN;
        
        // before "."
        for ( i = 0; i < dot_pos; i++ ) {
            if (src->name[i] <= 'z' && src->name[i] >= 'a' && (mode == 0x10 || mode == 0x00) )
                name[i] = src->name[i] - 'a' + 'A';
            else if (src->name[i] <= 'Z' && src->name[i] >= 'A' && (mode == 0x18 || mode == 0x08) )
                name[i] = src->name[i] - 'A' + 'a';
            else
                name[i] = src->name[i];
        }
        
        // after "."
        i = dot_pos;
        j = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN;
        if (src->name[j] != 0x20){
            name[i] = '.';
            for ( i = dot_pos + 1; j < MAX_FAT32_SHORT_FILE_NAME_LEN && src->name[j] != 0x20; i++, j++ ){
                if (src->name[j] <= 'z' && src->name[j] >= 'a' && (mode == 0x08 || mode == 0x00) )
                    name[i] = src->name[j] - 'a' + 'A';
                else if (src->name[j] <= 'Z' && src->name[j] >= 'A' && (mode == 0x18 || mode == 0x10))
                    name[i] = src->name[j] - 'A' + 'a';
                else
                    name[i] = src->name[j];
            }
            dest->len += 1;
        }
        
        name[i] = '\000';
        dest->name = name;
    }
   
    return;
}

// address_operation
// read a page(data, location, mapping(address space in inode))
u32 fat32_readpage(struct vfs_page *page)
{
    u32 err;
    u32 data_base;
    u32 abs_sect_addr;
    struct inode *inode;

    // sector ID
    // i_blksize == cluster size
    // cluster size / sector size = # sec per clu
    inode = page->p_mapping->a_inode;
    data_base = ((struct fat32_basic_information *)(inode->i_sb->s_fs_info))->fa_FAT->data_sec;
    abs_sect_addr = data_base + (page->p_location - 2) * (inode->i_blksize >> SECTOR_SHIFT);

    // read a cluster
    page->p_data = ( u8* )kmalloc(sizeof(u8) * inode->i_blksize);
    if (page->p_data == 0)
        return -ENOMEM;
    // how many sectors
    err = read_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);
    if (err)
        return -EIO;
    
    return 0;
}
// write page/cluster by inode.address space
// also by cluster ID
// == p_location
// inode->i_blksize == cluster size
// cluster size / sector size = # sec per clu
u32 fat32_writepage(struct vfs_page *page)
{
    struct inode *belonged_inode = page->p_mapping->a_inode;
    u32 data_sec = ((struct fat32_basic_information *)(belonged_inode->i_sb->s_fs_info))->fa_FAT->data_sec;
    u32 sec_id = data_sec + (page->p_location - 2) * (belonged_inode->i_blksize >> SECTOR_SHIFT);
    write_block(page->p_data, sec_id, belonged_inode->i_blksize >> SECTOR_SHIFT);

    return 0;
}

// page number of a file => cluster ID
u32 fat32_bmap(struct inode* file_inode, u32 page_number)
{
    return file_inode->i_addr.a_page[page_number];
}

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
    
    u32 dest_offset = index % (1u << shift);
    //u32 dest_index = index & (( 1u << shift ) - 1u );

    // 读扇区并取相应的项
    read_block(buffer, dest_sect, 1);
    return get_u32(buffer + (dest_offset << FAT32_FAT_ENTRY_LEN_SHIFT));
}