#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

#include <zjunix/utils.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/log.h>

// 外部变量
extern struct cache     *dcache;
extern struct dentry    *root_dentry;
extern struct dentry    *pwd_dentry;
extern struct vfsmount  *root_mnt;
extern struct vfsmount  *pwd_mnt;

static inline u32 parse_file_name(const u8 *full_name, int *index, u8 *buf, int buf_length);

/**
 * open it
 */
struct file* vfs_open(const unsigned char *filename, u32 flags, u32 mode){
    u32 namei_flags;
    u32 err;
    struct file *f;
    struct file_find_helper ffh;
    /**
     * get result in nd
     */
    err = open_namei(filename, namei_flags, mode, &ffh);

    if (!err)
        return dentry_open(ffh.this_dentry, ffh.mnt, flags);

    return ERR_PTR(err);
}

u32 open_namei(const u8 *pathname, u32 flag, u32 mode, struct file_find_helper *ffh){
    u32 err;
    u32 acc_mode;
    struct dentry *dir;
    struct dentry *dentry;

    err = path_lookup(pathname, LOOKUP_FOLLOW, ffh);
}

u32 path_lookup(const u8 * name, u32 flags, struct file_find_helper *ffh) {
    // root directory
    if ( *name == '/' ) {
        dget(root_dentry);
        ffh->mnt = root_mnt;
        ffh->this_dentry = root_dentry;
    }
    
    // pwd directory
    else {
        dget(pwd_dentry);
        ffh->mnt = pwd_mnt;
        ffh->this_dentry = pwd_dentry;
    }
    
    return link_path_walk(name, ffh);
}

u32 link_path_walk(const u8 *name, struct file_find_helper  *ffh)
{
    u32 err = 0;
    u8 buf[64];
    u32 buf_length = 64;
    int index = 0;

    struct dentry *parent_dentry = ffh->this_dentry;

    struct vfsmount *child_mount = ffh->mnt;
    struct dentry *child_dentry; // also parent_dentry
    // null char exit
    while(name[index] == '/') {
        u32 len = parse_file_name(name, &index, buf, buf_length);
        struct qstr dentry_name = (struct qstr){
            .name = buf,
            .len = len,
            .hash = 0xa5a5a5a5u
        };
        child_dentry = real_lookup(parent_dentry, &dentry_name, 0);
        if(child_dentry == 0) {
            err = 1;
            break;
        }
        // may be find failed with a simple dentry
        while(child_dentry->d_mounted) {
            // 2 level pointer for follow mount
            follow_mount(&child_mount, &child_dentry);
        }
        parent_dentry = child_dentry;
    }
    if(child_dentry != 0) {
        ffh->mnt = child_mount;
        ffh->this_dentry = child_dentry;
        return 0;
    } else {
        return err;
    }
}


struct dentry *real_lookup(struct dentry *parent, struct qstr *name, struct file_find_helper *ffh){
    struct dentry   *result = 0;
    struct inode    *dir = parent->d_inode;

    /**
     * new a naive dentry
     */
    struct dentry *dentry = d_alloc(parent, name);
    result = ERR_PTR(-ENOMEM);

    if (dentry) {
        result = parent->d_inode->i_op->lookup(dir, dentry, ffh);
        
        if (result)
            dput(dentry);
        else                    
            result = dentry;
    }
    
    return result;
}


// build a file
struct file * dentry_open(struct dentry *dentry, struct vfsmount *mnt, u32 flags) {
	struct file *f;
	struct inode *inode;
	u32 error;

	f = (struct file* )kmalloc(sizeof(struct file));
    INIT_LIST_HEAD(&f->f_list);
	if (!f) {
        dput(dentry);
        f =  ERR_PTR(error);
    }
    
    /**
     * build file
     */
    inode = dentry->d_inode;

	f->f_flags = flags;
	f->f_mode = ((flags+1) & O_ACCMODE) | FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE ;
	f->f_mapping = &(inode->i_addr);
	f->f_dentry = dentry;
	f->f_vfsmnt = mnt;
	f->f_pos = 0;
	f->f_op = inode->i_fop;
	f->f_flags &= ~(O_CREAT);

	return f;
}

// close it
u32 vfs_close(struct file *filp) {
	u32 err;

    // 把页高速缓存的数据（如果需要）全部写回磁盘    
	err = filp->f_op->flush(filp);
    if (!err)
        kfree(filp);

	return err;
}

/**
 * build a dentry using name
 * inode is not necessary
 * parent->inode->look_up do int
 */
struct dentry * d_alloc(struct dentry *parent, const struct qstr *name){  
    u8* dname;
    u32 i;
    struct dentry* dentry;  
    
    dentry = (struct dentry *) kmalloc ( sizeof(struct dentry) );
    if (!dentry)  
        return 0;
    
    dname = (u8*) kmalloc ( (name->len + 1)* sizeof(u8*) );
    kernel_memset(dname, 0, (name->len + 1));
    for ( i = 0; i < name->len; i++ ){
        dname[i] = name->name[i];
    }
    dname[i] == '\000';


    dentry->d_name.name         = dname;
    dentry->d_name.len          = name->len;   
    dentry->d_count             = 1;
    dentry->d_inode             = 0;  
    dentry->d_parent            = parent;
    dentry->d_sb                = parent->d_sb;
    dentry->d_op                = 0;
    
    INIT_LIST_HEAD(&dentry->d_hash);  
    INIT_LIST_HEAD(&dentry->d_LRU);  
    INIT_LIST_HEAD(&dentry->d_subdirs);
    INIT_LIST_HEAD(&(root_dentry->d_alias));

    if (parent) {
        dentry->d_parent = parent;
        dget(parent);
        dentry->d_sb = parent->d_sb;
        list_add(&dentry->d_child, &parent->d_subdirs);
    } else {
        INIT_LIST_HEAD(&dentry->d_child);
    }

    dcache->c_op->add(dcache, (void*)dentry);
    return dentry;
}

/**
 * @param *index == '/'
 */
static inline u32 parse_file_name(const u8 *full_name, int *index, u8 *buf, int buf_length)
{
    u32 count = 0;
    kernel_memset(buf, 0u, buf_length);
    while(*++index != '/' && count < buf_length - 1) {
        buf[count++] = full_name[*index];
    }
    return count;
}