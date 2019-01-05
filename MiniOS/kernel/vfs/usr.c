#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

#include <driver/vga.h>
#include <zjunix/utils.h>
#include <zjunix/slab.h>


extern struct cache *dcache;
extern struct cache *icache;
extern struct cache *pcache;
extern struct dentry *pwd_dentry;
extern struct vfsmount *pwd_mnt;

/**
 * cat it
 */
u32 vfs_cat(const u8 *path){
    u8 *buf;
    u32 err;
    u32 base;
    u32 file_size;
    struct file *file;
    
    // so easy
    file = vfs_open(path, O_RDONLY, base);
    if (IS_ERR_OR_NULL(file)){
        if ( PTR_ERR(file) == -ENOENT )
            kernel_printf("File not found!\n");
        return PTR_ERR(file);
    }
    
    // buf it
    base = 0;
    file_size = file->f_dentry->d_inode->i_size;
    
    buf = (u8*) kmalloc (file_size + 1);
    if ( vfs_read(file, buf, file_size, &base) != file_size )
        return 1;

    // output
    buf[file_size] = 0;
    kernel_printf("%s\n", buf);

    // close and output
    err = vfs_close(file);
    if (err)
        return err;
    
    kfree(buf);
    return 0;
}

/**
 * cat it
 */
u32 vfs_cd(const u8 *path){
    u32 err;
    struct file_find_helper ffh;

    // so easy
    err = path_lookup(path, LOOKUP_DIRECTORY, &ffh);
    if ( err == -ENOENT ){
        kernel_printf("No such directory!\n");
        return err;
    }
    else if ( IS_ERR_VALUE(err) ){
        return err;
    }

    // make a change
    pwd_dentry = ffh.this_dentry;
    pwd_mnt = ffh.mnt;

    return 0;
}

/**
 * ls it
 */
u32 vfs_ls(const u8 *path){
    u32 i;
    u32 err;
    struct file *file;
    struct getdent getdent;  

    // so easy
    if (path[0] == 0)
        file = vfs_open(".", LOOKUP_DIRECTORY, 0);
    else
        file = vfs_open(path, LOOKUP_DIRECTORY, 0);

    if (IS_ERR_OR_NULL(file)){
        if ( PTR_ERR(file) == -ENOENT )
            kernel_printf("Directory not found!\n");
        else
            kernel_printf("Other error: %d\n", -PTR_ERR(file));
        return PTR_ERR(file);
    }
    
    // so easy
    err = file->f_op->readdir(file, &getdent);
    if (err)
        return err;
    
    // otuput
    for ( i = 0; i < getdent.count; i++){
        if (getdent.dirent[i].type == FT_DIR)
            kernel_puts(getdent.dirent[i].name, VGA_CYAN, VGA_BLACK);
        else if(getdent.dirent[i].type == FT_REG_FILE)
            kernel_puts(getdent.dirent[i].name, VGA_WHITE, VGA_BLACK);
        else
            kernel_puts(getdent.dirent[i].name, VGA_GREEN, VGA_BLACK);
        kernel_printf(" ");
    }
    kernel_printf("\n");

    return 0;
}

/**
 * rm it
 */
u32 vfs_rm(const u8 *path){
    u32 err;
    struct inode        *inode;
    struct dentry       *dentry;
    struct file_find_helper ffh;

    // so easy
    err = path_lookup(path, 0, &ffh);
    if ( IS_ERR_VALUE(err) ){
        if ( err == -ENOENT )
            kernel_printf("File not found!\n");
        return err;
    }
    
    // so easy
    dentry = nd.dentry;
    err = dentry->d_inode->i_sb->s_op->delete_inode(dentry);
    if (err)
        return err;

    // 
    // leak of memory
    //
    dentry->d_inode = 0;

    return 0;
}