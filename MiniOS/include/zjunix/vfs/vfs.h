#ifndef _ZJUNIX_VFS_VFS_H
#define _ZJUNIX_VFS_VFS_H

#include "../type.h"
#include "../list.h"
#include "err.h"

#define DPT_MAX_ENTRY_COUNT                     4
#define DPT_ENTRY_LEN                           16
#define SECTOR_SIZE                             512
#define SECTOR_SHIFT                            9
#define BITS_PER_BYTE                           8

#define S_CLEAR                                 0
#define S_DIRTY                                 1

#define MAX_FILE_NAME_LEN                       255

#define D_PINNED                                1
#define D_UNPINNED                              0

// 文件打开方式，即open函数的入参flags。打开文件时用
#define O_RDONLY	                            0x0000                   // 为读而打开
#define O_WRONLY	                            0x0001                   // 为写而打开
#define O_RDWR		                            0x0002                   // 为读和写打开
#define O_ACCMODE	                            0x0003
#define O_CREAT		                            0x0100                   // 如果文件不存在，就创建它
#define O_APPEND	                            0x2000                   // 总是在文件末尾写
#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])

#define MAY_APPEND	                            0x0008

#define LOOKUP_FOLLOW                           0x0001                  // 如果最后一个分量是符号链接，则追踪（解释）它
#define LOOKUP_DIRECTORY                        0x0002                  // 最后一个分量必须是目录
#define LOOKUP_CONTINUE                         0x0004                  // 在路径名中还有文件名要检查
#define LOOKUP_PARENT                           0x0010                  // 查找最后一个分量所在的目录
#define LOOKUP_CREATE                           0x0200                  // 试图创建一个文件

// LOOKUP_PARENT 中最后分量的类型
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT, LAST_BIND};

// 文件打开方式。打开文件时后用
#define FMODE_READ		                        0x1                     // 文件为读而打开
#define FMODE_WRITE		                        0x2                     // 文件为写而打开
#define FMODE_LSEEK		                        0x4                     // 文件可以寻址
#define FMODE_PREAD		                        0x8                     // 文件可用pread
#define FMODE_PWRITE	                        0x10                    // 文件可用pwrite

#define PAGE_SIZE                               (1 << PAGE_SHIFT)
#define PAGE_SHIFT                              12
#define PAGE_CACHE_SIZE                         PAGE_SIZE
#define PAGE_CACHE_SHIFT                        PAGE_SHIFT
#define PAGE_CACHE_MASK                         (~((1 << PAGE_SHIFT) - 1))

// 文件类型
enum {
         FT_UNKNOWN,     
         FT_REG_FILE,    
         FT_DIR    
};

// 由于C的编译需要
struct vfs_page;

// 主引导记录
struct master_boot_record {
    u32                                 m_count;                        // 分区数
    u32                                 m_base[DPT_MAX_ENTRY_COUNT];    // 每个分区的基地址
    u8                                  m_data[SECTOR_SIZE];            // 数据
};

// 记录文件系统类型信息
struct file_system_type {
    u8                                  *name;                  // 名称
};

// 超级块
struct super_block {
    u8                                  s_dirt;                 // 修改标志
    u32                                 s_blksize;              // 以字节为单位的块大小
    struct file_system_type             *s_type;                // 文件系统类型
    struct dentry                       *s_root;                // 文件系统根目录的目录项对象
    struct super_operations             *s_op;                  // 超级块的操作函数
    void                                *s_fs_info;             // 指向特定文件系统的超级块信息的指针
};

// 挂载信息
struct vfsmount {
	struct list_head                    mnt_hash;               // 用于散列表的指针
	struct vfsmount                     *mnt_parent;	        // 指向父文件系统， 这个文件安装在其上
	struct dentry                       *mnt_mountpoint;        // 指向这个文件系统安装点目录的dentry
	struct dentry                       *mnt_root;              // 指向这个文件系统根目录的dentry
	struct super_block                  *mnt_sb;                // 指向这个文件系统的超级块对象
};

// 已缓存的页
struct address_space {
    u32                                 a_pagesize;             // 页大小(字节)
    
    // size/length == inode.i_blocks
    // u32 for a pointer address
    u32                                 *a_page;                // 文件页到逻辑页的映射表
    struct inode                        *a_inode;                // 相关联的inode
    struct list_head                    a_cache;                // 已缓冲的页链表
    struct address_space_operations     *a_op;                  // 操作函数
};

// 文件节点
struct inode {
    u32                                 i_ino;                  // 索引节点号
    u32                                 i_count;                // 当前的引用计数
    u32                                 i_blocks;               // 块数
    u32                                 i_blkbits;              // 用于移位
    u32                                 i_blksize;              // 块的字节数
    u32                                 i_size;                 // 对应文件的字节数
    struct list_head                    i_hash;                 // 用于散列链表的指针
    struct list_head                    i_LRU;                  // 用于LRU链表的指针
    struct list_head                    i_dentry;               // 引用索引节点的目录项链表的头
    struct inode_operations             *i_op;                  // 操作函数
    struct file_operations              *i_fop;                 // 对应的文件操作函数
    struct super_block                  *i_sb;                  // 指向超级块对象的指针
    struct address_space                i_addr;                 // 文件的地址空间对象
};

// 字符串包装
struct qstr {
    const u8                            *name;                  // 字符串
    u32                                 len;                    // 长度
    u32                                 hash;                   // 哈希值
};

// 目录项
struct dentry {
    u32                                 d_count;                // 当前的引用计数
    u32                                 d_pinned;               // （额外）是否被锁定（一般为根目录）
    u32                                 d_mounted;              // 对目录而言，记录安装该目录项的文件系统数的计数器
    struct inode                        *d_inode;               // 与文件名相关的索引节点
    struct list_head                    d_hash;                 // 指向散列表表项的指针
    struct dentry                       *d_parent;              // 父目录的目录项对象
    struct qstr                         d_name;                 // 文件名
    struct list_head                    d_LRU;                  // 用于未使用目录项链表的指针
    struct list_head                    d_child;                // 对目录而言，用于同一父目中目录项链表的指针
    struct list_head                    d_subdirs;              // 对目录而言，子目录项链表的头
    struct list_head                    d_alias;                // 用于与同一索引节点相关的目录项链表的指针
    struct dentry_operations            *d_op;                  // 目录项方法
    struct super_block                  *d_sb;                  // 文件的超级块对象
};

// 打开的文件
struct file {
    u32                                 f_pos;                  // 文件当前的读写位置
	struct list_head	                f_list;                 // 用于通用文件对象链表的指针
	struct dentry		                *f_dentry;              // 与文件相关的目录项对象
	struct vfsmount                     *f_vfsmnt;              // 含有该文件的已安装文件系统
	struct file_operations	            *f_op;                  // 指向文件操作表的指针
	u32 		                        f_flags;                // 当打开文件时所指定的标志
	u32			                        f_mode;                 // 进程的访问方式
	struct address_space	            *f_mapping;             // 指向文件地址空间对象的指针
};

// 打开用
struct open_intent {
    u32	                                flags;
	u32	                                create_mode;
};

// // 寻找目录用结构一
// struct nameidata {  
//     struct dentry                       *dentry;                // 对应目录项
//     struct vfsmount                     *mnt;                   // 对应文件系统挂载项
//     struct qstr                         last;                   // 最后一个分量的名字
//     u32                                 flags;                  // 打开标记
//     u32                                 last_type;              // 最后一个分量的文件类型
//     union {                                                     // 打开用
// 		struct open_intent open;
// 	} intent;
// };

struct file_find_helper {
  	struct vfsmount *mnt;       // mount
  	struct dentry *this_dentry; // entry of directory
};

// 通用目录项信息
struct dirent {
    u32                                 ino;                    // inode 号
    u8                                  type;                   // 文件类型
    const u8                            *name;                  // 文件名
};

// 目录项获取结构体
struct getdent {
    u32                                 count;                  // 目录项数
    struct dirent                       *dirent;                // 目录项数组
};

// super operations
struct super_operations {
    u32 (*delete_inode) (struct dentry *);
    u32 (*write_inode) (struct inode *, struct dentry *);
};

// file primitives
struct file_operations {  
    u32 (*read)(struct file *, u8 *, u32, u32 *);   
    u32 (*write)(struct file *, u8 *, u32, u32 *);   
    u32 (*flush)(struct file *);   
    u32 (*readdir)(struct file *, struct getdent *);
};

// inode
struct inode_operations {
    u32 (*create)(struct inode *,struct dentry *, u32, struct file_find_helper *);
    struct dentry *(*lookup)(struct inode *,struct dentry *, struct file_find_helper *);
};

// address space == block/cluster/page
struct address_space_operations {
    u32 (*writepage)(struct vfs_page *);
    u32 (*readpage)(struct vfs_page *);
    u32 (*bmap)(struct inode *, u32);
};

// directory
struct dentry_operations {
    u32 (*compare)(const struct qstr *, const struct qstr *);
};

// vfs.c
u32 init_vfs();
u32 vfs_read_MBR();

// open.c
struct file *vfs_open(const u8 *, u32, u32);
u32 open_namei(const u8 *, u32, u32, struct file_find_helper *ffh);
u32 path_lookup(const u8 *, u32 , struct file_find_helper *ffh);
u32 link_path_walk(const u8 *, struct file_find_helper *ffh);
// void follow_dotdot(struct vfsmount **, struct dentry **);
// u32 do_lookup(struct nameidata *, struct qstr *, struct path *);
struct dentry *real_lookup(struct dentry *, struct qstr *, struct file_find_helper *ffh);
// struct dentry *__lookup_hash(struct qstr *, struct dentry *, struct nameidata *);
struct dentry *d_alloc(struct dentry *, const struct qstr *);
struct file *dentry_open(struct dentry *, struct vfsmount *, u32);
u32 vfs_close(struct file *);

// read_write.c
u32 vfs_read(struct file *file, char *buf, u32 count, u32 *pos);
u32 vfs_write(struct file *file, char *buf, u32 count, u32 *pos);
u32 generic_file_read(struct file *, u8 *, u32, u32 *);
u32 generic_file_write(struct file *, u8 *, u32, u32 *);
u32 generic_file_flush(struct file *);

// mount.c
u32 mount_ext2();
u32 follow_mount(struct vfsmount **, struct dentry **);
struct vfsmount *lookup_mnt(struct vfsmount *, struct dentry *);

// utils.c
u16 get_u16(u8 *);
u32 get_u32(u8 *);
void set_u16(u8 *, u16);
void set_u32(u8 *, u32);
u32 read_block(u8 *, u32, u32);
u32 write_block(u8 *, u32, u32);
u32 generic_compare_filename(const struct qstr *, const struct qstr *);
u32 get_bit(const u8 *, u32);
void set_bit(u8 *, u32);
void reset_bit(u8 *, u32);

// usr.c
u32 vfs_cat(const u8 *);
u32 vfs_cd(const u8 *);
u32 vfs_ls(const u8 *);
u32 vfs_rm(const u8 *);



#endif