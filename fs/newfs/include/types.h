#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
 * SECTION: Type def
 *******************************************************************************/
typedef int boolean;
typedef uint16_t flag16;

typedef enum file_type
{
    NFS_FILE, // 普通文件
    NFS_DIR   // 目录文件
} FILE_TYPE;

/******************************************************************************
 * SECTION: Macro
 *******************************************************************************/
#define TRUE 1
#define FALSE 0
#define UINT32_BITS 32
#define UINT8_BITS 8

#define NFS_MAGIC_NUM 0x52415453
#define NFS_SUPER_OFS 0
#define NFS_ROOT_INO 0

#define NFS_ERROR_NONE 0
#define NFS_ERROR_ACCESS EACCES
#define NFS_ERROR_SEEK ESPIPE
#define NFS_ERROR_ISDIR EISDIR
#define NFS_ERROR_NOSPACE ENOSPC
#define NFS_ERROR_EXISTS EEXIST
#define NFS_ERROR_NOTFOUND ENOENT
#define NFS_ERROR_UNSUPPORTED ENXIO
#define NFS_ERROR_IO EIO       /* Error Input/Output */
#define NFS_ERROR_INVAL EINVAL /* Invalid Args */

#define NFS_MAX_FILE_NAME 128
// 一个逻辑块里面可以放16个inode
#define NFS_INODE_PER_FILE 16
// 一个文件最多使用4个数据块
#define NFS_DATA_PER_FILE 6
#define NFS_DEFAULT_PERM 0777

#define NFS_IOC_MAGIC 'S'
#define NFS_IOC_SEEK _IO(NFS_IOC_MAGIC, 0)

#define NFS_FLAG_BUF_DIRTY 0x1
#define NFS_FLAG_BUF_OCCUPY 0x2

/**磁盘布局设计 */
// 超级块
#define NFS_BLKS_SUPER 1
// 索引节点位图
#define NFS_BLKS_MAP_INODE 1
// 数据区位图
#define NFS_BLKS_MAP_DATA 1
#define NFS_BLKS_INODE 585
#define NFS_BLKS_DATA 3508

/******************************************************************************
 * SECTION: Macro Function
 *******************************************************************************/
// 固定大小等信息
#define NFS_IO_SZ() (nfs_super.sz_io)
#define NFS_BLK_SZ() (nfs_super.sz_blks)
#define NFS_DISK_SZ() (nfs_super.sz_disk)
#define NFS_DRIVER() (nfs_super.fd)
//!!!!
#define NFS_BLKS_SZ(blks) ((blks) * NFS_BLK_SZ())
// 计算一个磁盘块可以储存多少dentry
#define NFS_DENTRY_PER_DATABLK() (NFS_BLK_SZ() / sizeof(struct nfs_dentry)) 
// 向下取整
#define NFS_ROUND_DOWN(value, round) ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
// 向上取整
#define NFS_ROUND_UP(value, round) ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))
// 修改文件名称
#define NFS_ASSIGN_FNAME(pnfs_dentry, _fname) memcpy(pnfs_dentry->fname, _fname, strlen(_fname))
// 偏移的计算
#define NFS_INO_OFS(ino) (nfs_super.inode_offset + NFS_BLKS_SZ(ino))
#define NFS_DATA_OFS(dno) (nfs_super.data_offset + NFS_BLKS_SZ(dno))
// 判断inode类型
#define NFS_IS_DIR(pinode) (pinode->dentry->ftype == NFS_DIR)
#define NFS_IS_REG(pinode) (pinode->dentry->ftype == NFS_FILE)

/******************************************************************************
 * SECTION: FS Specific Structure - In memory structure
 *******************************************************************************/
struct custom_options
{
    const char *device;
};

struct nfs_inode
{
    uint32_t ino;                          // 索引编号
    int size;                              // 文件已占用的空间
    int link;                              // 链接数
    FILE_TYPE ftype;                       // 文件类型
    int used_block_num[NFS_DATA_PER_FILE]; // 该inode对应的文件占的数据块的块号
    uint8_t *data[NFS_DATA_PER_FILE];      // 指向数据块的指针
    struct nfs_dentry *dentry;             // 指向该inode的dentry
    struct nfs_dentry *dentrys;            // 所有目录项
    int dir_cnt;                           // 如果是目录类型文件，下面有几个目录项
};

struct nfs_dentry {
    char                    fname[NFS_MAX_FILE_NAME];   // 文件名
    uint32_t                ino;                        // 指向的ino号 
    FILE_TYPE               ftype;                      // 文件类型
    struct nfs_dentry*      parent;                     // 父目录的dentry
    struct nfs_dentry*      brother;                    // 兄弟的dentry
    struct nfs_inode*       inode;                      // 文件对应的inode
};

struct nfs_super {
    uint32_t           magic_num;       // 幻数
    int                fd;              // 设备描述符

    int                sz_io;           // 驱动的IO大小：512B
    int                sz_blks;         // EXT2的磁盘块大小：1024B
    int                sz_disk;         // 虚拟磁盘容量：4MB
    int                sz_usage;

    int                max_ino;         // 索引节点占块最大数目
    uint8_t*           map_inode;       // inode位图
    int                map_inode_blks;  // inode位图所占的数据块
    int                map_inode_offset;// inode位图的起始地址
    int                inode_offset;    // 索引节点的起始地址

    int                max_data;        // 数据块最大数目
    uint8_t*           map_data;        // data位图
    int                map_data_offset; // data位图的起始地址
    int                map_data_blks;   // data位图所占的块数
    int                data_offset;     // 数据块的起始地址
    
    boolean            is_mounted;      // 是否挂载
    struct nfs_dentry* root_dentry;     // 根目录
};

/* 创建目录项 */
static inline struct nfs_dentry* new_dentry(char * fname, FILE_TYPE ftype) {
    struct nfs_dentry * dentry = (struct nfs_dentry *)malloc(sizeof(struct nfs_dentry));
    memset(dentry, 0, sizeof(struct nfs_dentry));
    NFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;  
    return dentry;                                          
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct nfs_super_d {
    uint32_t           magic_num;           // 幻数
    int                sz_usage;

    int                map_inode_blks;      // inode位图所占的块数
    int                map_inode_offset;    // inode位图的起始地址

    int                map_data_offset;     // data位图的起始地址
    int                map_data_blks;       // data位图所占的块数

    int                data_offset;         // 数据块的起始地址
    int                inode_offset;        // 索引节点的起始地址
};

struct nfs_inode_d {
    uint32_t           ino;                             // 索引编号
    int                size;                            // 文件已占用空间
    int                link;                            // 链接数，默认为1
    FILE_TYPE          ftype;                           // 文件类型（目录类型、普通文件类型）
    int                used_block_num[NFS_DATA_PER_FILE];// 数据块指针（可固定分配）
    int                dir_cnt;                         // 如果是目录类型文件，下面有几个目录项
};

struct nfs_dentry_d {
    char            fname[NFS_MAX_FILE_NAME];            // 文件名
    uint32_t        ino;                                // 指向的ino号
    FILE_TYPE       ftype;                              // 文件类型
};

#endif /* _TYPES_H_ */