#include "../include/newfs.h"

struct nfs_super nfs_super;
struct custom_options nfs_options;

/**
 * @brief 获取文件名
 *
 * @param path
 * @return char*
 */
char *nfs_get_fname(const char *path)
{
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path
 * @return int
 */
int nfs_calc_lvl(const char *path)
{
    char *str = path;
    int lvl = 0;
    // 比较给定路径 path 是否与根路径“/”相等。如果相等，则表示路径只有根目录，不包含其他层级，直接返回层级数 lvl。
    if (strcmp(path, "/") == 0)
    {
        return lvl;
    }
    // 如果指针str指向的字符不是空字符（即路径结尾），则循环判断
    while (*str != NULL)
    {
        if (*str == '/')
        {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 *
 * @param offset
 * @param out_content
 * @param size
 * @return int
 */
int nfs_driver_read(int offset, uint8_t *out_content, int size)
{
    // 按照一个逻辑块大小(1024B)封装
    int offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;

    // 把磁盘头到down位置
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);

    // 按照磁盘块大小(512B)去读
    while (size_aligned != 0)
    {
        // read(NFS_DRIVER(), cur, NFS_IO_SZ());
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 驱动写
 *
 * @param offset
 * @param in_content
 * @param size
 * @return int
 */
int nfs_driver_write(int offset, uint8_t *in_content, int size)
{
    // 按照一个逻辑块大小(1024B)封装
    int offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;
    // 读出需要的磁盘块到内存
    nfs_driver_read(offset_aligned, temp_content, size_aligned);
    // 在内存覆盖指定内容
    memcpy(temp_content + bias, in_content, size);

    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);

    // 将读出的磁盘块再依次写回到内存
    while (size_aligned != 0)
    {
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 为一个inode分配dentry，采用头插法
 *
 * @param inode
 * @param dentry
 * @return int
 */
int nfs_alloc_dentry(struct nfs_inode *inode, struct nfs_dentry *dentry)
{
    if (inode->dentrys == NULL)
    {
        inode->dentrys = dentry;
    }
    else
    {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;

    // 如果是文件，还需要检查当前indn对应的数据块有没有满
    if (dentry->ftype == NFS_DIR && (inode->dir_cnt % NFS_DENTRY_PER_DATABLK() == 1))
    {
        int cur_blk = inode->dir_cnt / NFS_DENTRY_PER_DATABLK();
        int byte_cursor, bit_cursor, dno_cursor;
        boolean find_free_blk = FALSE;
        for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_data_blks); byte_cursor++)
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
            {
                if ((nfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0)
                {
                    /* 当前dno_cursor位置空闲 */
                    nfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);

                    inode->used_block_num[cur_blk] = dno_cursor;
                    find_free_blk = TRUE;
                    break;
                }
                dno_cursor++;
            }
            if (find_free_blk)
            {
                break;
            }
        }
        if (!find_free_blk || dno_cursor == nfs_super.max_data)
        {
            return -NFS_ERROR_NOSPACE;
        }
    }
    return inode->dir_cnt;
}

/**
 * @brief 分配一个inode，占用索引位图；
 * 如果dentry是文件类型，还开辟内存，但未修改数据位图
 * @param dentry 该dentry指向分配的inode
 * @return nfs_inode
 */
struct nfs_inode *nfs_alloc_inode(struct nfs_dentry *dentry)
{
    struct nfs_inode *inode;
    int byte_cursor = 0;
    int bit_cursor = 0;
    int ino_cursor = 0; // 记录inode块号
    boolean is_find_free_entry = FALSE;

    // 在索引节点位图上查找空闲的索引节点
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks); byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            if ((nfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                /* 当前ino_cursor位置空闲 */
                // 更新位图
                nfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry)
        {
            break;
        }
    }

    // 未找到空闲的inode
    if (!is_find_free_entry || ino_cursor == nfs_super.max_ino)
    {
        return -NFS_ERROR_NOSPACE;
    }

    // 找到了则为该dentry分配一个inode
    inode = (struct nfs_inode *)malloc(sizeof(struct nfs_inode));
    inode->ino = ino_cursor;
    inode->size = 0;
    inode->dir_cnt = 0;
    inode->dentry = dentry;
    inode->dentrys = NULL;

    dentry->inode = inode;
    dentry->ino = inode->ino;

    // 如果是文件类型的话，要分配data指针指向的存储空间
    if (NFS_IS_REG(inode))
    {
        for (int i; i < NFS_DATA_PER_FILE; i++)
        {
            inode->data[i] = (uint8_t *)malloc(NFS_BLK_SZ());
        }
    }
    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 *
 * @param inode
 * @return int
 */
int nfs_sync_inode(struct nfs_inode *inode)
{
    struct nfs_inode_d inode_d;
    struct nfs_dentry *dentry_cursor;
    struct nfs_dentry_d dentry_d;
    int ino = inode->ino;
    inode_d.ino = ino;
    inode_d.size = inode->size;
    inode_d.ftype = inode->dentry->ftype;
    inode_d.dir_cnt = inode->dir_cnt;
    printf("*****back to disk %s\n", inode->dentry->fname);
    for (int i = 0; i < NFS_DATA_PER_FILE; i++)
    {
        inode_d.used_block_num[i] = inode->used_block_num[i];
    }
    // 将inode_d本身写入磁盘
    if (nfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d,
                         sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE)
    {
        NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;
    }

    // inode是一个普通文件索引
    if (NFS_IS_REG(inode))
    {      
        printf("*****back to disk is file %s\n", inode->dentry->fname);
        for (int i = 0; i < NFS_DATA_PER_FILE; i++)
        {
            if (nfs_driver_write(NFS_DATA_OFS(inode->used_block_num[i]), inode->data[i], NFS_BLK_SZ()) != NFS_ERROR_NONE)
            {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;
            }
        }
    }
    // inode是一个指向文件夹的索引，要遍历里面每一个dentry对应的inode。会有递归
    else if (NFS_IS_DIR(inode))
    {   
        printf("*****back to disk is dir %s\n", inode->dentry->fname);
        int offset;
        int blk_number = 0;
        dentry_cursor = inode->dentrys;
        // 遍历一个inode对应的所有data块
        while (dentry_cursor != NULL && blk_number < NFS_DATA_PER_FILE)
        {
            offset = NFS_DATA_OFS(inode->used_block_num[blk_number]); // 当前数据块的offset
            // 遍历当前数据块的每一个dentry
            while ((dentry_cursor != NULL) && (offset < NFS_DATA_OFS(inode->used_block_num[blk_number] + 1)))
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, NFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (nfs_driver_write(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE)
                {
                    NFS_DBG("[%s] io error\n", __func__);
                    return -NFS_ERROR_IO;
                }
                if (dentry_cursor->inode != NULL)
                {   
                    nfs_sync_inode(dentry_cursor->inode);
                }
                dentry_cursor = dentry_cursor->brother; // 开始遍历下一个兄弟
                offset += sizeof(struct nfs_dentry_d);
            }
            blk_number++;
        }
    }
    return NFS_ERROR_NONE;
}

struct nfs_dentry *nfs_get_dentry(struct nfs_inode *inode, int dir)
{
    struct nfs_dentry *dentry_cursor = inode->dentrys;
    int cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt)
        {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

struct nfs_dentry *nfs_lookup(const char *path, boolean *is_find, boolean *is_root)
{
    struct nfs_dentry *dentry_cursor = nfs_super.root_dentry;
    struct nfs_dentry *dentry_ret = NULL;
    struct nfs_inode *inode;
    int total_lvl = nfs_calc_lvl(path);
    int lvl = 0;
    boolean is_hit;
    char *fname = NULL;
    char *path_cpy = (char *)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    /* 如果路径的级数为0，则说明是根目录，直接返回根目录项即可 */
    if (total_lvl == 0)
    {
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = nfs_super.root_dentry;
    }

    /* 获取最外层文件夹名称 */
    fname = strtok(path_cpy, "/");
    while (fname)
    {
        lvl++;

        /* Cache机制，如果当前dentry对应的inode为空，则从磁盘中读取 */
        if (dentry_cursor->inode == NULL)
        {
            nfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        /* 当前dentry对应的inode */
        inode = dentry_cursor->inode;

        /* 若当前inode对应文件类型，且还没查找到对应层数，说明路径错误，跳出循环 */
        if (NFS_IS_REG(inode) && lvl < total_lvl)
        {
            NFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        /* 若当前inode对应文件夹 */
        if (NFS_IS_DIR(inode))
        {
            dentry_cursor = inode->dentrys;
            is_hit = FALSE;

            /* 遍历该目录下所有目录项 */
            while (dentry_cursor)
            {
                /* 如果名称匹配，则命中 */
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0)
                {
                    is_hit = TRUE;
                    break;
                }
                // 否则查找下一个目录项
                dentry_cursor = dentry_cursor->brother;
            }

            /* 若未命中 */
            if (!is_hit)
            {
                *is_find = FALSE;
                NFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            /* 若命中 */
            if (is_hit && lvl == total_lvl)
            {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        /* 获取下一层文件夹名称 */
        fname = strtok(NULL, "/");
    }

    /* 如果对应dentry的inode还没读进来，则重新读 */
    if (dentry_ret->inode == NULL)
    {
        dentry_ret->inode = nfs_read_inode(dentry_ret, dentry_ret->ino);
    }

    return dentry_ret;
}

/**
 * @brief 挂载nfs,
 * 16个Inode占用一个Blk
 * @param options
 * @return int
 */
int nfs_mount(struct custom_options options)
{
    int ret = NFS_ERROR_NONE;
    int driver_fd;
    struct nfs_super_d nfs_super_d;
    struct nfs_dentry *root_dentry;
    struct nfs_inode *root_inode;

    // inode
    int inode_num;
    int map_inode_blks;

    // data
    int data_num;
    int map_data_blks;

    int super_blks;
    boolean is_init = FALSE;

    nfs_super.is_mounted = FALSE;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0)
    {
        return driver_fd;
    }

    // 往超级块中写入信息 fd + 三个大小
    nfs_super.fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &nfs_super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &nfs_super.sz_io);
    nfs_super.sz_blks = 2 * nfs_super.sz_io;

    // 创建根目录
    root_dentry = new_dentry("/", NFS_DIR);

    // 读取磁盘super_d
    if (nfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&nfs_super_d), sizeof(struct nfs_super_d)) != NFS_ERROR_NONE)
    {
        return -1;
    }

    // 判断是否是是第一次挂载
    if (nfs_super_d.magic_num != NFS_MAGIC_NUM)
    {   // 第一次挂载
        // 初始化大小
        printf("*************************first mount\n`");
        super_blks = NFS_BLKS_SUPER;
        inode_num = NFS_BLKS_INODE;
        data_num = NFS_BLKS_DATA;
        map_data_blks = NFS_BLKS_MAP_DATA;
        map_inode_blks = NFS_BLKS_MAP_INODE;

        // 初始化布局
        nfs_super.max_ino = inode_num;
        nfs_super.max_data = data_num;

        // 更新super_d
        nfs_super_d.map_inode_blks = map_inode_blks;
        nfs_super_d.map_data_blks = map_data_blks;

        nfs_super_d.map_inode_offset = NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks);
        nfs_super_d.map_data_offset = nfs_super_d.map_inode_offset + NFS_BLKS_SZ(map_inode_blks);

        nfs_super_d.inode_offset = nfs_super_d.map_data_offset + NFS_BLKS_SZ(map_data_blks);
        nfs_super_d.data_offset = nfs_super_d.inode_offset + NFS_BLKS_SZ(inode_num);

        nfs_super_d.sz_usage = 0;
        nfs_super_d.magic_num = NFS_MAGIC_NUM;
        is_init = TRUE;
    }

    // 建立in memeory结构，即从磁盘中读取的已经完成了初始化。用磁盘中的super块初始化内存中的super块
    nfs_super.sz_usage = nfs_super_d.sz_usage;

    // 建立inode位图（仅仅开辟空间）
    nfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_inode_blks));
    nfs_super.map_inode_blks = nfs_super_d.map_inode_blks;
    nfs_super.map_inode_offset = nfs_super_d.map_inode_offset;
    //inode区偏移
    nfs_super.inode_offset = nfs_super_d.inode_offset;

    // 建立data位图（仅仅开辟空间）
    nfs_super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_data_blks));
    nfs_super.map_data_blks = nfs_super_d.map_data_blks;
    nfs_super.map_data_offset = nfs_super_d.map_data_offset;
    //data区偏移
    nfs_super.data_offset = nfs_super_d.data_offset;

    // 将磁盘中位图信息复制进来
    if (nfs_driver_read(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE)
    {
        return -1;
    }
    if (nfs_driver_read(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE)
    {
        return -1;
    }
    // 如果是第一次挂载，为根dentry分配一个指向其的inode
    if (is_init)
    {
        root_inode = nfs_alloc_inode(root_dentry);
        nfs_sync_inode(root_inode);
    }
    root_inode = nfs_read_inode(root_dentry, NFS_ROOT_INO);
    root_dentry->inode = root_inode;
    nfs_super.root_dentry = root_dentry;
    nfs_super.is_mounted = TRUE;

    return ret;
}

/**
 * @brief
 *
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct nfs_inode*
 */
struct nfs_inode *nfs_read_inode(struct nfs_dentry *dentry, int ino)
{
    struct nfs_inode *inode = (struct nfs_inode *)malloc(sizeof(struct nfs_inode));
    struct nfs_inode_d inode_d;
    struct nfs_dentry *sub_dentry;
    struct nfs_dentry_d dentry_d;
    int dir_cnt = 0;

    /* 从磁盘中读取ino对应的inode_d */
    if (nfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d,
                        sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE)
    {
        NFS_DBG("[%s] io error\n", __func__);
        return NULL;
    }

    /* 根据inode_d更新内存中inode参数 */
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for (int i = 0; i < NFS_DATA_PER_FILE; i++)
    {
        inode->used_block_num[i] = inode_d.used_block_num[i];
    }

    /*判断iNode节点的文件类型*/
    /*如果inode是目录类型，则需要读取每一个目录项并建立连接*/
    if (NFS_IS_DIR(inode))
    {
        dir_cnt = inode_d.dir_cnt;
        int blk_number = 0;
        int offset;

        /*处理每一个目录项*/
        while (dir_cnt > 0 && blk_number < NFS_DATA_PER_FILE)
        {
            offset = NFS_DATA_OFS(inode->used_block_num[blk_number]);

            // 当从磁盘读入时，由于磁盘中没有链表指针，因此只能通过一个dentry_d大小来进行遍历
            while ((dir_cnt > 0) && (offset + sizeof(struct nfs_dentry_d) < NFS_DATA_OFS(inode->used_block_num[blk_number] + 1)))
            {
                if (nfs_driver_read(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE)
                {
                    NFS_DBG("[%s] io error\n", __func__);
                    return NULL;
                }

                /* 从磁盘中读出的dentry_d更新内存中的sub_dentry */
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino = dentry_d.ino;
                nfs_alloc_dentry(inode, sub_dentry);

                offset += sizeof(struct nfs_dentry_d);
                dir_cnt--;
            }
            blk_number++;
        }
    }
    /*如果inode是文件类型，则直接读取数据即可*/
    else if (NFS_IS_REG(inode))
    {
        for (int i = 0; i < NFS_DATA_PER_FILE; i++)
        {
            inode->data[i] = (uint8_t *)malloc(NFS_BLK_SZ());
            if (nfs_driver_read(NFS_DATA_OFS(inode->used_block_num[i]), (uint8_t *)inode->data[i],
                                NFS_BLK_SZ()) != NFS_ERROR_NONE)
            {
                NFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
        }
    }

    return inode;
}

int nfs_umount()
{
    struct nfs_super_d nfs_super_d;

    if (!nfs_super.is_mounted)
    {
        return NFS_ERROR_NONE;
    }
    // 写回的是索引节点部分和数据块部分
    nfs_sync_inode(nfs_super.root_dentry->inode);
    // 内存中超级快更新将写回磁盘的超级快，并将super_d写回
    nfs_super_d.magic_num = NFS_MAGIC_NUM;
    nfs_super_d.sz_usage = nfs_super.sz_usage;

    nfs_super_d.map_inode_blks = nfs_super.map_inode_blks;
    nfs_super_d.map_inode_offset = nfs_super.map_inode_offset;
    nfs_super_d.inode_offset = nfs_super.inode_offset;

    nfs_super_d.map_data_blks = nfs_super.map_data_blks;
    nfs_super_d.map_data_offset = nfs_super.map_data_offset;
    nfs_super_d.data_offset = nfs_super.data_offset;

    if (nfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&nfs_super_d, sizeof(struct nfs_super_d)) != NFS_ERROR_NONE)
    {
        return -NFS_ERROR_IO;
    }

    // 将inode位图写回
    if (nfs_driver_write(nfs_super_d.map_inode_offset, (uint8_t *)nfs_super.map_inode, NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE)
    {
        return -NFS_ERROR_IO;
    }

    // 将data位图写回
    if (nfs_driver_write(nfs_super_d.map_data_offset, (uint8_t *)nfs_super.map_data, NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE)
    {
        return -NFS_ERROR_IO;
    }
    free(nfs_super.map_inode);
    free(nfs_super.map_data);
    ddriver_close(NFS_DRIVER());
    return NFS_ERROR_NONE;
}
