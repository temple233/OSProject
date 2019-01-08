#include <driver/vga.h>
#include <zjunix/log.h>
#include <zjunix/slab.h>
#include "fat.h"

u8 mk_dir_buf[32];
FILE file_create;

extern u8 cwd_name_fat[64];

/* remove directory entry */
u32 fs_rm_fat(u8 *filename)
{
    u32 clus;
    u32 next_clus;
    FILE mk_dir;

    if (fs_open_fat(&mk_dir, filename) == 1)
        goto fs_rm_err;

    /* Mark 0xE5 */
    mk_dir.entry.data[0] = 0xE5;

    /* Release all allocated block */
    clus = get_start_cluster(&mk_dir);

    while (clus != 0 && clus <= fat_info.total_data_clusters + 1) {
        if (get_fat_entry_value(clus, &next_clus) == 1)
            goto fs_rm_err;

        if (fs_modify_fat(clus, 0) == 1)
            goto fs_rm_err;

        clus = next_clus;
    }

    if (fs_close_fat(&mk_dir) == 1)
        goto fs_rm_err;

    return 0;
fs_rm_err:
    return 1;
}

/* move directory entry */
u32 fs_mv_fat(u8 *src, u8 *dest)
{
    u32 i;
    FILE mk_dir;
    u8 filename11[13];

    /* if src not exists */
    if (fs_open_fat(&mk_dir, src) == 1)
        goto fs_mv_err;

    /* create dest */
    if (fs_create_with_attr(dest, mk_dir.entry.data[11]) == 1)
        goto fs_mv_err;

    /* copy directory entry */
    for (i = 0; i < 32; i++)
        mk_dir_buf[i] = mk_dir.entry.data[i];

    /* new path */
    for (i = 0; i < 11; i++)
        mk_dir_buf[i] = filename11[i];

    if (fs_open_fat(&file_create, dest) == 1)
        goto fs_mv_err;

    /* copy directory entry to dest */
    for (i = 0; i < 32; i++)
        file_create.entry.data[i] = mk_dir_buf[i];

    if (fs_close_fat(&file_create) == 1)
        goto fs_mv_err;

    /* mark src directory entry 0xE5 */
    mk_dir.entry.data[0] = 0xE5;

    if (fs_close_fat(&mk_dir) == 1)
        goto fs_mv_err;

    return 0;
fs_mv_err:
    return 1;
}

/* mkdir, create a new file and write . and .. */
u32 fs_mkdir_fat(u8 *filename)
{
    u32 i;
    FILE mk_dir;
    FILE file_creat;

    if (fs_create_with_attr(filename, 0x30) == 1)
        goto fs_mkdir_err;

    if (fs_open_fat(&mk_dir, filename) == 1)
        goto fs_mkdir_err;

    mk_dir_buf[0] = '.';
    for (i = 1; i < 11; i++)
        mk_dir_buf[i] = 0x20;

    mk_dir_buf[11] = 0x30;
    for (i = 12; i < 32; i++)
        mk_dir_buf[i] = 0;

    if (fs_write_fat(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;

    fs_lseek_fat(&mk_dir, 0);

    mk_dir_buf[20] = mk_dir.entry.data[20];
    mk_dir_buf[21] = mk_dir.entry.data[21];
    mk_dir_buf[26] = mk_dir.entry.data[26];
    mk_dir_buf[27] = mk_dir.entry.data[27];

    if (fs_write_fat(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;

    mk_dir_buf[0] = '.';
    mk_dir_buf[1] = '.';

    for (i = 2; i < 11; i++)
        mk_dir_buf[i] = 0x20;

    mk_dir_buf[11] = 0x30;
    for (i = 12; i < 32; i++)
        mk_dir_buf[i] = 0;

    set_u16(mk_dir_buf + 20, (file_creat.dir_entry_pos >> 16) & 0xFFFF);
    set_u16(mk_dir_buf + 26, file_creat.dir_entry_pos & 0xFFFF);

    if (fs_write_fat(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;

    for (i = 28; i < 32; i++)
        mk_dir.entry.data[i] = 0;

    if (fs_close_fat(&mk_dir) == 1)
        goto fs_mkdir_err;

    return 0;
fs_mkdir_err:
    return 1;
}

u32 fs_cat_fat(u8 *path)
{
    u8 filename[12];
    FILE cat_file;

    /* Open */
    if (0 != fs_open_fat(&cat_file, path)) {
        log(LOG_FAIL, "File %s open failed", path);
        return 1;
    }

    /* Read */
    u32 file_size = get_entry_filesize_fat(cat_file.entry.data);
    u8 *buf = (u8 *)kmalloc(file_size + 1);
    fs_read_fat(&cat_file, buf, file_size);
    buf[file_size] = 0;
    kernel_printf("%s\n", buf);
    fs_close_fat(&cat_file);
    kfree(buf);
    return 0;

}

char *cut_front_blank(char *str)
{
    char *s = str;
    unsigned int index = 0;

    while (*s == ' ') {
        ++s;
        ++index;
    }

    if (!index)
        return str;

    while (*s) {
        *(s - index) = *s;
        ++s;
    }

    --s;
    *s = 0;

    return str;
}

static inline unsigned int strlen(unsigned char *str)
{
    unsigned int len = 0;
    while (str[len])
        ++len;
    return len;
}

static inline unsigned int each_param(char *para, char *word, unsigned int off, char ch)
{
    int index = 0;

    while (para[off] && para[off] != ch) {
        word[index] = para[off];
        ++index;
        ++off;
    }

    word[index] = 0;

    return off;
}

u32 fs_ls_fat(u8 *para)
{
    struct dir_entry_attr entry;
    char name[32];
    char *p = para;
    // unsigned int next;
    unsigned int r;
    unsigned int p_len;
    FS_FAT_DIR dir;

    p = cut_front_blank(p);
    p_len = strlen(p);
    // next = each_param(p, cwd_name_fat, 0, ' ');

    if (fs_open_dir_fat(&dir, cwd_name_fat)) {
        kernel_printf("open dir(%s) failed : No such directory!\n", cwd_name_fat);
        return 1;
    }

readdir:
    r = fs_read_dir_fat(&dir, (unsigned char *)&entry);
    if (1 != r) {
        if (-1 == r) {
            kernel_printf("\n");
        } else {
            get_filename((unsigned char *)&entry, name);
            if (entry.attr == 0x10)  // sub dir
                kernel_printf("%s/", name);
            else
                kernel_printf("%s", name);
            kernel_printf("\n");
            goto readdir;
        }
    } else
        return 1;

    return 0;
}

u32 fs_cd_fat(u8 *dirName)
{
    u32 len = strlen(cwd_name_fat);
    for(int i = 0; i < strlen(dirName); i++) {
        cwd_name_fat[len + i] = dirName[i];
    }
    // success
    return 0;
}