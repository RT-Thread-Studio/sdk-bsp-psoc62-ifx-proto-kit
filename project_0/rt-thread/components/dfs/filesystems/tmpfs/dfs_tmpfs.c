/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2013-04-15     Bernard      the first version
 * 2013-05-05     Bernard      remove CRC for tmpfs persistence
 * 2013-05-22     Bernard      fix the no entry issue.
 */

#include <rtthread.h>
#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <lwp.h>
#include <lwp_user_mm.h>

#include "dfs_tmpfs2.h"

struct tmpfs_sb *sb_list = RT_NULL;

int dfs_tmpfs_mount(struct dfs_filesystem *fs,
                    unsigned long          rwflag,
                    const void            *data)
{
    struct tmpfs_sb *superblock;

    superblock = rt_calloc(1, sizeof(struct tmpfs_sb));
    superblock->df_size = 0;
    superblock->magic = TMPFS_MAGIC;
    rt_list_init(&superblock->sibling);

    superblock->root.sb = superblock;
    superblock->root.type = TMPFS_TYPE_DIR;
    rt_list_init(&superblock->root.sibling);
    rt_list_init(&superblock->root.subdirs);
    
    if (sb_list == RT_NULL)
    {
        sb_list = superblock;
    }
    else
    {
        /* todo mutli superblock */
    }

    fs->data = superblock;

    return RT_EOK;
}

int dfs_tmpfs_unmount(struct dfs_filesystem *fs)
{
    fs->data = NULL;

    return RT_EOK;
}

int dfs_tmpfs_statfs(struct dfs_filesystem *fs, struct statfs *buf)
{
    // struct tmpfs_sb *superblock;

    // tmpfs = (struct dfs_tmpfs *)fs->data;
    // RT_ASSERT(tmpfs != NULL);
    // RT_ASSERT(buf != NULL);

    // buf->f_bsize  = 512;
    // buf->f_blocks = tmpfs->memheap.pool_size / 512;
    // buf->f_bfree  = tmpfs->memheap.available_size / 512;

    return RT_EOK;
}

int dfs_tmpfs_ioctl(struct dfs_fd *file, int cmd, void *args)
{
    // struct tmpfs_flie *dirent;
    // struct tmpfs_sb *superblock;

    // dirent = (struct tmpfs_flie *)file->fnode->data;
    // RT_ASSERT(dirent != NULL);

    // tmpfs = dirent->fs;
    // RT_ASSERT(tmpfs != NULL);

    // switch (cmd)
    // {
    //     case RT_FIOMMAP2: 
    //     {
    //         struct dfs_mmap2_args *mmap2 = (struct dfs_mmap2_args *)args;
    //         if (mmap2)
    //         {
    //             if (mmap2->length > file->fnode->size)
    //             {
    //                 return -RT_ENOMEM;
    //             }

    //             rt_kprintf("tmpfile mmap ptr:%x , size:%d\n", dirent->data, mmap2->length);
    //             mmap2->ret = lwp_map_user_phy(lwp_self(), RT_NULL, dirent->data, mmap2->length, 0);
    //         }
    //         return RT_EOK;
    //         break;
    //     }
    // }
    return -EIO;
}

struct tmpfs_file *dfs_tmpfs_lookup(struct tmpfs_sb  *superblock,
                                      const char       *path,
                                      rt_size_t        *size)
{
    const char *subpath;
    struct tmpfs_file *file;

    subpath = path;
    while (*subpath == '/' && *subpath)
        subpath ++;
    if (! *subpath) /* is root directory */
    {
        *size = 0;

        return &(superblock->root);
    }

    for (file = rt_list_entry(superblock->root.sibling.next, struct tmpfs_file, sibling);
         file != &(superblock->root);
         file = rt_list_entry(file->sibling.next, struct tmpfs_file, sibling))
    {
        if (rt_strcmp(file->name, subpath) == 0)
        {
            *size = file->size;

            return file;
        }
    }

    /* not found */
    return NULL;
}

int dfs_tmpfs_read(struct dfs_fd *file, void *buf, size_t count)
{
    rt_size_t length;
    struct tmpfs_file *d_file;

    d_file = (struct tmpfs_file *)file->fnode->data;
    RT_ASSERT(d_file != NULL);

    if (count < file->fnode->size - file->pos)
        length = count;
    else
        length = file->fnode->size - file->pos;

    if (length > 0)
        memcpy(buf, &(d_file->data[file->pos]), length);

    /* update file current position */
    file->pos += length;

    return length;
}


int dfs_tmpfs_write(struct dfs_fd *fd, const void *buf, size_t count)
{
    struct tmpfs_flie *d_file;
    struct tmpfs_sb *superblock;

    d_file = (struct tmpfs_flie *)fd->fnode->data;
    RT_ASSERT(d_file != NULL);

    superblock = d_file->sb;
    RT_ASSERT(superblock != NULL);

    if (count + fd->pos > fd->fnode->size)
    {
        rt_uint8_t *ptr;
        ptr = rt_realloc(d_file->data, fd->pos + count);
        if (ptr == NULL)
        {
            rt_set_errno(-ENOMEM);

            return 0;
        }

        superblock->df_size += (fd->pos - d_file->size + count);
        /* update d_file and file size */
        d_file->data = ptr;
        d_file->size = fd->pos + count;
        fd->fnode->size = d_file->size;
        rt_kprintf("tmpfile ptr:%x, size:%d\n", ptr, d_file->size);
    }

    if (count > 0)
        memcpy(d_file->data + fd->pos, buf, count);

    /* update file current position */
    fd->pos += count;

    return count;
}

int dfs_tmpfs_lseek(struct dfs_fd *file, off_t offset)
{
    if (offset <= (off_t)file->fnode->size)
    {
        file->pos = offset;

        return file->pos;
    }

    return -EIO;
}

int dfs_tmpfs_close(struct dfs_fd *file)
{
    // RT_ASSERT(file->fnode->ref_count > 0);
    // if (file->fnode->ref_count > 1)
    // {
    //     return 0;
    // }

    // file->fnode->data = NULL;

    return RT_EOK;
}

int dfs_tmpfs_open(struct dfs_fd *file)
{
    rt_size_t size;
    struct tmpfs_sb *superblock;
    struct tmpfs_file *d_file, *p_file;
    struct dfs_filesystem *fs;

    RT_ASSERT(file->fnode->ref_count > 0);
    if (file->fnode->ref_count > 1)
    {
        if (file->fnode->type == FT_DIRECTORY
                && !(file->flags & O_DIRECTORY))
        {
            return -ENOENT;
        }
        file->pos = 0;
        return 0;
    }

    fs = file->fnode->fs;

    superblock = (struct tmpfs_sb *)fs->data;
    RT_ASSERT(superblock != NULL);

    if (file->flags & O_DIRECTORY)
    {
        if (file->flags & O_CREAT)
        {
            /* todo */
            return -ENOSPC;
        }

        /* open directory */
        d_file = dfs_tmpfs_lookup(superblock, file->fnode->path, &size);
        if (d_file == NULL)
            return -ENOENT;
        if (d_file == &(superblock->root)) /* it's root directory */
        {
            if (!(file->flags & O_DIRECTORY))
            {
                return -ENOENT;
            }
        }
        file->fnode->type = FT_DIRECTORY;
    }
    else
    {
        d_file = dfs_tmpfs_lookup(superblock, file->fnode->path, &size);
    //     if (d_file == &(tmpfs->root)) /* it's root directory */
    //     {
    //         return -ENOENT;
    //     }

        if (d_file == NULL)
        {
            if (file->flags & O_CREAT || file->flags & O_WRONLY)
            {
                char *name_ptr;

                /* create a file entry */
                d_file = (struct tmpfs_file *)rt_calloc(1, sizeof(struct tmpfs_file));
                if (d_file == NULL)
                {
                    return -ENOMEM;
                }

                /* todo: find parent file */
                p_file = RT_NULL;

                /* remove '/' separator */
                name_ptr = file->fnode->path;
                while (*name_ptr == '/' && *name_ptr)
                {
                    name_ptr++;
                }
                strncpy(d_file->name, name_ptr, TMPFS_NAME_MAX);

                rt_list_init(&(d_file->subdirs));
                rt_list_init(&(d_file->sibling));
                d_file->data = NULL;
                d_file->size = 0;
                d_file->sb = superblock;
                file->fnode->type = FT_DEVICE;

                if (p_file == RT_NULL)
                {
                    /* add to the root directory */
                    rt_list_insert_after(&(superblock->root.sibling), &(d_file->sibling));
                }
                else
                {
                    /* todo add to patent dir */
                }
            }
            else
                return -ENOENT;
        }

    //     /* Creates a new file.
    //      * If the file is existing, it is truncated and overwritten.
    //      */
    //     if (file->flags & O_TRUNC)
    //     {
    //         d_file->size = 0;
    //         if (d_file->data != NULL)
    //         {
    //             rt_memheap_free(d_file->data);
    //             d_file->data = NULL;
    //         }
    //     }
    }

    file->fnode->data = d_file;
    file->fnode->size = d_file->size;
    if (file->flags & O_APPEND)
    {
        file->pos = file->fnode->size;
    }
    else
    {
        file->pos = 0;
    }

    return 0;
}

int dfs_tmpfs_stat(struct dfs_filesystem *fs,
                   const char            *path,
                   struct stat           *st)
{
    rt_size_t size;
    struct tmpfs_file *d_file;
    struct tmpfs_sb *superblock;

    superblock = (struct tmpfs_sb *)fs->data;
    d_file = dfs_tmpfs_lookup(superblock, path, &size);

    if (d_file == NULL)
        return -ENOENT;

    st->st_dev = 0;
    st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH |
                  S_IWUSR | S_IWGRP | S_IWOTH;

    st->st_size = d_file->size;
    st->st_mtime = 0;

    return RT_EOK;
}

int dfs_tmpfs_getdents(struct dfs_fd *file,
                       struct dirent *dirp,
                       uint32_t    count)
{
    rt_size_t index, end;
    struct dirent *d;
    struct tmpfs_file *d_file, *n_file;
    rt_list_t *list;
    struct tmpfs_sb *superblock;

    d_file = (struct tmpfs_flie *)file->fnode->data;

    superblock  = d_file->sb;
    RT_ASSERT(superblock != RT_NULL);

    if (d_file != &(superblock->root))
        return -EINVAL;

    /* make integer count */
    count = (count / sizeof(struct dirent));
    if (count == 0)
        return -EINVAL;

    end = file->pos + count;
    index = 0;
    count = 0;

    
    rt_list_for_each(list, &d_file->sibling)
    {
        n_file = rt_container_of(list, struct tmpfs_file, sibling);
        if (index >= (rt_size_t)file->pos)
        {
            d = dirp + count;
            d->d_type = DT_REG;
            d->d_namlen = RT_NAME_MAX;
            d->d_reclen = (rt_uint16_t)sizeof(struct dirent);
            rt_strncpy(d->d_name, n_file->name, TMPFS_NAME_MAX);

            count += 1;
            file->pos += 1;
        }
        index += 1;
        if (index >= end)
        {
            break;
        }
    }

    // for (d_file = rt_list_entry(d_file->sibling.next, struct tmpfs_flie, sibling);
    //      d_file != &(superblock->root) && index < end;
    //      d_file = rt_list_entry(d_file->sibling.next, struct tmpfs_flie, sibling))
    // {
    //     if (index >= (rt_size_t)file->pos)
    //     {
    //         d = dirp + count;
    //         d->d_type = DT_REG;
    //         d->d_namlen = RT_NAME_MAX;
    //         d->d_reclen = (rt_uint16_t)sizeof(struct dirent);
    //         rt_strncpy(d->d_name, d_file->name, TMPFS_NAME_MAX);

    //         count += 1;
    //         file->pos += 1;
    //     }
    //     index += 1;
    // }

    return count * sizeof(struct dirent);
}

int dfs_tmpfs_unlink(struct dfs_filesystem *fs, const char *path)
{
    // rt_size_t size;
    // struct tmpfs_sb *superblock;
    // struct tmpfs_flie *dirent;

    // tmpfs = (struct dfs_tmpfs *)fs->data;
    // RT_ASSERT(tmpfs != NULL);

    // dirent = dfs_tmpfs_lookup(tmpfs, path, &size);
    // if (dirent == NULL)
    //     return -ENOENT;

    // rt_list_remove(&(dirent->list));
    // if (dirent->data != NULL)
    //     rt_memheap_free(dirent->data);
    // rt_memheap_free(dirent);

    return RT_EOK;
}

int dfs_tmpfs_rename(struct dfs_filesystem *fs,
                     const char            *oldpath,
                     const char            *newpath)
{
    // struct tmpfs_flie *dirent;
    // struct tmpfs_sb *superblock;
    // rt_size_t size;

    // tmpfs = (struct dfs_tmpfs *)fs->data;
    // RT_ASSERT(tmpfs != NULL);

    // dirent = dfs_tmpfs_lookup(tmpfs, newpath, &size);
    // if (dirent != NULL)
    //     return -EEXIST;

    // dirent = dfs_tmpfs_lookup(tmpfs, oldpath, &size);
    // if (dirent == NULL)
    //     return -ENOENT;

    // strncpy(dirent->name, newpath, TMPFS_NAME_MAX);

    return RT_EOK;
}

static const struct dfs_file_ops _tmp_fops =
{
    dfs_tmpfs_open,
    dfs_tmpfs_close,
    dfs_tmpfs_ioctl,
    dfs_tmpfs_read,
    dfs_tmpfs_write,
    NULL, /* flush */
    dfs_tmpfs_lseek,
    dfs_tmpfs_getdents,
};

static const struct dfs_filesystem_ops _tmpfs =
{
    "tmp",
    DFS_FS_FLAG_DEFAULT,
    &_tmp_fops,

    dfs_tmpfs_mount,
    dfs_tmpfs_unmount,
    NULL, /* mkfs */
    dfs_tmpfs_statfs,

    dfs_tmpfs_unlink,
    dfs_tmpfs_stat,
    dfs_tmpfs_rename,
};

int dfs_tmpfs_init(void)
{
    /* register tmp file system */
    dfs_register(&_tmpfs);

    return 0;
}
INIT_COMPONENT_EXPORT(dfs_tmpfs_init);

// struct dfs_tmpfs *dfs_tmpfs_create(rt_uint8_t *pool, rt_size_t size)
// {
//     struct tmpfs_sb *superblock;
//     rt_uint8_t *data_ptr;
//     rt_err_t result;

//     size  = RT_ALIGN_DOWN(size, RT_ALIGN_SIZE);
//     tmpfs = (struct dfs_tmpfs *)pool;

//     data_ptr = (rt_uint8_t *)(tmpfs + 1);
//     size = size - sizeof(struct dfs_tmpfs);
//     size = RT_ALIGN_DOWN(size, RT_ALIGN_SIZE);

//     result = rt_memheap_init(&tmpfs->memheap, "tmpfs", data_ptr, size);
//     if (result != RT_EOK)
//         return NULL;
//     /* detach this memheap object from the system */
//     rt_object_detach((rt_object_t) & (tmpfs->memheap));

//     /* initialize tmpfs object */
//     tmpfs->magic = TMPFS_MAGIC;
//     tmpfs->memheap.parent.type = RT_Object_Class_MemHeap | RT_Object_Class_Static;

//     /* initialize root directory */
//     memset(&(tmpfs->root), 0x00, sizeof(tmpfs->root));
//     rt_list_init(&(tmpfs->root.list));
//     tmpfs->root.size = 0;
//     strcpy(tmpfs->root.name, ".");
//     tmpfs->root.fs = tmpfs;

//     return tmpfs;
// }

