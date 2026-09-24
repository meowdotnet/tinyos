#include "fs.h"
#include "lib.h"
#include "memory.h"
#include "process.h"

/*
 * TinyFS (TFS) — RAM-backed core-UNIX filesystem.
 *
 * A "block" is one physical page.  The superblock and a table of inode pages
 * are allocated at mount; data pages are allocated on demand.  The superblock
 * reserved[] doubles as the inode-number -> page table because the bitmap page
 * allocator hands out non-contiguous frames.
 *
 * The kernel copies to/from user buffers (it never maps fs pages into ring 3),
 * so read/write are copy-based like stdio.
 *
 * Concepts: filesystem -> directories -> directory entries -> files -> fds.
 * Deliberately absent: journaling, indirect blocks, copy-on-write, ext4-style
 * feature sprawl.
 */

static struct tfs_superblock *sb;
static int mounted;
static uint32_t tfs_clock;
static unsigned char selftest_buf[TFS_BLOCK_SIZE + 37u];

#define SB_INODE_MAP(sb) ((uint32_t *)(sb)->reserved)

/* ---------------------------------------------------------------- helpers */

static uint32_t now(void)
{
    return ++tfs_clock;
}

static struct tfs_inode *inode_at(uint32_t ino)
{
    if (!mounted || !sb || ino >= sb->max_inodes)
        return 0;
    return (struct tfs_inode *)SB_INODE_MAP(sb)[ino];
}

static unsigned int dirent_reclen(unsigned int namelen)
{
    return (8u + namelen + 3u) & ~3u;
}

static int is_dir(const struct tfs_inode *in)
{
    return (in->mode & TFS_IFMT) == TFS_IFDIR;
}

static unsigned int file_block(struct tfs_inode *in, unsigned int index,
                               int alloc);

/* v1 runs every process as root; retain owner/group/other checks for later. */
static int allowed(const struct tfs_inode *in, unsigned int want /* 4=x 2=w 1=r */)
{
    struct process *p = process_current();
    uint32_t uid = p ? p->uid : 0u;
    uint32_t gid = p ? p->gid : 0u;
    uint32_t bits;
    unsigned int x_bit, w_bit, r_bit;

    if (!in)
        return 0;
    if (uid == 0u)
        return 1;
    if (uid == in->uid) {
        x_bit = 0100u;
        w_bit = 0200u;
        r_bit = 0400u;
    } else if (gid == in->gid) {
        x_bit = 0010u;
        w_bit = 0020u;
        r_bit = 0040u;
    } else {
        x_bit = 0001u;
        w_bit = 0002u;
        r_bit = 0004u;
    }
    bits = in->mode;
    if ((want & 4) && !(bits & x_bit)) return 0;
    if ((want & 2) && !(bits & w_bit)) return 0;
    if ((want & 1) && !(bits & r_bit)) return 0;
    return 1;
}

/* Find `name` in directory `dir`; returns inode number or -1. */
static int dir_lookup(const struct tfs_inode *dir, const char *name,
                      unsigned int namelen)
{
    unsigned int off;

    for (off = 0; off < dir->size;) {
        unsigned int bno = off / TFS_BLOCK_SIZE;
        unsigned int boff = off % TFS_BLOCK_SIZE;
        struct tfs_dirent *d;

        /* dir_add never lets a record straddle pages; skip its tail padding. */
        if (boff + 8u > TFS_BLOCK_SIZE) {
            off = (bno + 1u) * TFS_BLOCK_SIZE;
            continue;
        }
        if (bno >= TFS_DIRECT_BLOCKS || !dir->blocks[bno])
            break;
        d = (struct tfs_dirent *)(dir->blocks[bno] + boff);
        if (d->reclen < 8u) {
            if (boff) {
                off = (bno + 1u) * TFS_BLOCK_SIZE;
                continue;
            }
            break;
        }
        if ((d->reclen & 3u) || d->reclen > TFS_BLOCK_SIZE - boff ||
            off + d->reclen > dir->size ||
            d->namelen > d->reclen - 8u || d->inode >= sb->max_inodes)
            break;
        {
            struct tfs_inode *target = inode_at(d->inode);
            if (!target || (target->mode & TFS_IFMT) >> 12 != d->type)
                break;
        }
        if (d->namelen == namelen && kmemcmp(d->name, name, namelen) == 0)
            return (int)d->inode;
        off += d->reclen;
    }
    return -1;
}

/* Append a directory entry; allocates a new data page if the record would
 * straddle a page boundary. */
static int dir_add(struct tfs_inode *dir, uint32_t ino, const char *name,
                   unsigned int namelen, unsigned char type)
{
    unsigned int need;
    unsigned int bno;
    unsigned int boff;
    unsigned int page;
    struct tfs_dirent *d;

    if (!dir || !name || namelen == 0 || namelen > TFS_NAME_MAX ||
        ino >= sb->max_inodes)
        return TFS_EINVAL;
    {
        struct tfs_inode *target = inode_at(ino);
        if (!target || (unsigned char)(target->mode >> 12) != type)
            return TFS_EINVAL;
    }
    need = dirent_reclen(namelen);
    bno = dir->size / TFS_BLOCK_SIZE;
    boff = dir->size % TFS_BLOCK_SIZE;

    if (bno >= TFS_DIRECT_BLOCKS)
        return TFS_ENOSPC;
    /* If the record would straddle a page boundary, move to a fresh page. */
    if (boff + need > TFS_BLOCK_SIZE) {
        bno++;
        boff = 0;
        if (bno >= TFS_DIRECT_BLOCKS)
            return TFS_ENOSPC;
    }
    /* Allocate the target page on demand (handles an empty dir's first page). */
    page = file_block(dir, bno, 1);
    if (!page)
        return TFS_ENOSPC;

    d = (struct tfs_dirent *)(page + boff);
    d->inode = ino;
    d->reclen = (unsigned short)need;
    d->type = type;
    d->namelen = (unsigned char)namelen;
    kmemcpy(d->name, name, namelen);
    dir->size = bno * TFS_BLOCK_SIZE + boff + need;
    dir->mtime = dir->ctime = now();
    return TFS_OK;
}

static int alloc_inode(uint32_t mode, uint32_t parent, uint32_t *out_ino)
{
    uint32_t i;
    for (i = 0; i < sb->max_inodes; i++) {
        struct tfs_inode *in = inode_at(i);
        if (in->mode == 0) {
            kmemset(in, 0, sizeof(*in));
            in->mode = mode;
            in->uid = 0;
            in->gid = 0;
            in->parent = parent;
            in->nlink = ((mode & TFS_IFMT) == TFS_IFDIR) ? 2u : 1u;
            in->atime = in->mtime = in->ctime = now();
            sb->used_inodes++;
            *out_ino = i;
            return TFS_OK;
        }
    }
    return TFS_ENOSPC;
}

/* Locate data block `index` of a file, optionally allocating it. */
static unsigned int file_block(struct tfs_inode *in, unsigned int index,
                               int alloc)
{
    if (index >= TFS_DIRECT_BLOCKS)
        return 0;
    if (!in->blocks[index]) {
        unsigned int page;
        if (!alloc)
            return 0;
        page = memory_alloc_page();
        if (!page)
            return 0;
        in->blocks[index] = page;
        kmemset((void *)page, 0, TFS_BLOCK_SIZE);
        sb->used_blocks++;
    }
    return in->blocks[index];
}

static void truncate_inode(struct tfs_inode *in)
{
    unsigned int i;
    for (i = 0; i < TFS_DIRECT_BLOCKS; i++) {
        if (in->blocks[i]) {
            memory_free_page(in->blocks[i]);
            in->blocks[i] = 0;
            if (sb->used_blocks)
                sb->used_blocks--;
        }
    }
    in->size = 0;
    in->mtime = in->ctime = now();
}

/*
 * Resolve a path.  Relative paths are rooted at / until TinyFS grows a
 * per-process current directory.  Returns the inode number, or a negative
 * error.  `*leaf_name`/`*leaf_len` receive the final path component (for
 * create).
 */
static int resolve(const char *path, const char **leaf_name,
                   unsigned int *leaf_len, uint32_t *leaf_parent)
{
    uint32_t cur = TFS_ROOT_INODE;
    const char *p = path;
    unsigned int path_len;
    int require_dir;

    if (!path || !*path)
        return TFS_EINVAL;
    path_len = kstrlen(path);
    require_dir = path[path_len - 1u] == '/';
    if (leaf_name)
        *leaf_name = 0;
    if (*p == '/') {
        while (*p == '/')
            p++;
    }

    while (*p) {
        const char *start = p;
        unsigned int len;
        int next;
        struct tfs_inode *dir;

        while (*p && *p != '/')
            p++;
        len = (unsigned int)(p - start);
        while (*p == '/')
            p++;
        if (len > TFS_NAME_MAX)
            return TFS_ENAMETOOLONG;

        /* Every component, including . and .., starts in a directory. */
        dir = inode_at(cur);
        if (!dir || !is_dir(dir))
            return TFS_ENOTDIR;
        if (!allowed(dir, 4))
            return TFS_EACCES;

        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            struct tfs_inode *parent = inode_at(dir->parent);
            if (!parent || !is_dir(parent) || !allowed(parent, 4))
                return TFS_EIO;
            cur = dir->parent;
            continue;
        }

        next = dir_lookup(dir, start, len);
        if (next < 0) {
            if (!*p) {                    /* missing final component */
                if (require_dir)
                    return TFS_ENOTDIR;
                if (leaf_name) {
                    *leaf_name = start;
                    *leaf_len = len;
                    *leaf_parent = cur;
                }
                return TFS_ENOENT;         /* caller may create here */
            }
            return TFS_ENOENT;
        }
        cur = (uint32_t)next;
    }

    {
        struct tfs_inode *final_inode = inode_at(cur);
        if (require_dir && (!final_inode || !is_dir(final_inode)))
            return TFS_ENOTDIR;
    }
    return (int)cur;
}

/* Release every page owned by the current RAM-backed filesystem. */
static void discard_filesystem(void)
{
    uint32_t *map;
    unsigned int i, b;

    if (!sb) {
        mounted = 0;
        return;
    }
    map = SB_INODE_MAP(sb);
    for (i = 0; i < sb->max_inodes; i++) {
        struct tfs_inode *in = (struct tfs_inode *)map[i];
        if (!in)
            continue;
        for (b = 0; b < TFS_DIRECT_BLOCKS; b++) {
            if (in->blocks[b]) {
                memory_free_page(in->blocks[b]);
                in->blocks[b] = 0;
            }
        }
        kmemset(in, 0, sizeof(*in));
    }
    for (i = 0; i < sb->max_inodes; i++) {
        if (map[i]) {
            memory_free_page(map[i]);
            map[i] = 0;
        }
    }
    memory_free_page(sb->sb_page);
    sb = 0;
    mounted = 0;
}

/* ---------------------------------------------------------------- mount */

int tfs_format(void)
{
    unsigned int sbp = 0;
    unsigned int pages[TFS_MAX_INODES];
    unsigned int allocated = 0;
    unsigned int i;
    uint32_t *map;

    discard_filesystem();
    sbp = memory_alloc_page();
    if (!sbp)
        return TFS_ENOSPC;
    sb = (struct tfs_superblock *)sbp;
    kmemset((void *)sbp, 0, TFS_BLOCK_SIZE);

    sb->magic = TFS_MAGIC;
    sb->block_size = TFS_BLOCK_SIZE;
    sb->sb_page = sbp;
    sb->max_inodes = TFS_MAX_INODES;
    sb->root_inode = TFS_ROOT_INODE;
    sb->used_inodes = 0;
    sb->used_blocks = 0;

    map = SB_INODE_MAP(sb);
    for (i = 0; i < TFS_MAX_INODES; i++) {
        pages[i] = memory_alloc_page();
        if (!pages[i])
            goto fail;
        map[i] = pages[i];
        allocated++;
    }
    return TFS_OK;

fail:
    while (allocated)
        memory_free_page(pages[--allocated]);
    memory_free_page(sbp);
    sb = 0;
    return TFS_ENOSPC;
}

int tfs_mount(void)
{
    static const char *std[] = { "bin", "dev", "etc", "home", "tmp", "usr" };
    struct tfs_inode *root;
    unsigned int i;
    int rc;

    rc = tfs_format();
    if (rc != TFS_OK)
        return rc;
    mounted = 1;

    root = inode_at(TFS_ROOT_INODE);
    kmemset(root, 0, sizeof(*root));
    root->mode = TFS_IFDIR | 0755u;
    root->uid = root->gid = 0;
    root->parent = TFS_ROOT_INODE;
    root->nlink = 2;
    root->atime = root->mtime = root->ctime = now();
    sb->used_inodes = 1;

    for (i = 0; i < sizeof(std) / sizeof(std[0]); i++) {
        uint32_t ino;
        rc = alloc_inode(TFS_IFDIR | 0755u, TFS_ROOT_INODE, &ino);
        if (rc != TFS_OK) {
            discard_filesystem();
            return rc;
        }
        rc = dir_add(root, ino, std[i], kstrlen(std[i]), TFS_IFDIR >> 12);
        if (rc != TFS_OK) {
            discard_filesystem();
            return rc;
        }
        root->nlink++;
    }
    return TFS_OK;
}

/* ---------------------------------------------------------------- files */

int tfs_open(const char *path, uint32_t flags, struct tfs_open_file *out)
{
    const char *leaf = 0;
    unsigned int leaf_len = 0;
    uint32_t parent = 0;
    uint32_t ino = 0;
    struct tfs_inode *in;
    unsigned int access = flags & TFS_O_ACCMODE;
    int want_read = access != TFS_O_WRONLY;
    int want_write = access != TFS_O_RDONLY;
    int rc;

    if (!mounted || !out || access > TFS_O_RDWR ||
        (flags & ~(TFS_O_ACCMODE | TFS_O_CREAT | TFS_O_TRUNC | TFS_O_APPEND)))
        return TFS_EINVAL;
    if (((flags & TFS_O_TRUNC) && !want_write) ||
        ((flags & TFS_O_APPEND) && !want_write))
        return TFS_EACCES;

    rc = resolve(path, &leaf, &leaf_len, &parent);
    if (rc == TFS_ENOENT) {
        struct tfs_inode *pdir;
        if (!(flags & TFS_O_CREAT) || !leaf)
            return TFS_ENOENT;
        pdir = inode_at(parent);
        if (!pdir || !is_dir(pdir))
            return TFS_ENOTDIR;
        if (!allowed(pdir, 2))
            return TFS_EACCES;
        if (dir_lookup(pdir, leaf, leaf_len) >= 0)
            return TFS_EEXIST;
        if (alloc_inode(TFS_IFREG | 0644u, parent, &ino) != TFS_OK)
            return TFS_ENOSPC;
        rc = dir_add(pdir, ino, leaf, leaf_len, TFS_IFREG >> 12);
        if (rc != TFS_OK) {
            in = inode_at(ino);
            if (in) {
                in->mode = 0;
                sb->used_inodes--;
            }
            return rc;
        }
    } else if (rc < 0) {
        return rc;
    } else {
        ino = (uint32_t)rc;
    }

    in = inode_at(ino);
    if (!in)
        return TFS_ENOENT;
    if (is_dir(in) && want_write)
        return TFS_EISDIR;
    if (want_read && !allowed(in, 1))
        return TFS_EACCES;
    if (want_write && !allowed(in, 2))
        return TFS_EACCES;
    if (flags & TFS_O_TRUNC)
        truncate_inode(in);

    out->in_use = 1;
    out->inode = ino;
    out->offset = (flags & TFS_O_APPEND) ? in->size : 0;
    out->flags = flags;
    return TFS_OK;
}

int tfs_close(struct tfs_open_file *f)
{
    if (!f || !f->in_use)
        return TFS_EBADF;
    f->in_use = 0;
    f->inode = 0;
    f->offset = 0;
    f->flags = 0;
    return TFS_OK;
}

int tfs_read(struct tfs_open_file *f, void *buf, uint32_t count,
             uint32_t *nread)
{
    struct tfs_inode *in;
    uint32_t done = 0;

    if (!nread)
        return TFS_EBADF;
    *nread = 0;
    if (!f || !f->in_use)
        return TFS_EBADF;
    if (!buf && count)
        return TFS_EINVAL;
    if ((f->flags & TFS_O_ACCMODE) == TFS_O_WRONLY)
        return TFS_EACCES;
    in = inode_at(f->inode);
    if (!in)
        return TFS_EBADF;
    if (is_dir(in))
        return TFS_EISDIR;

    while (done < count && f->offset < in->size) {
        unsigned int bno = f->offset / TFS_BLOCK_SIZE;
        unsigned int boff = f->offset % TFS_BLOCK_SIZE;
        unsigned int chunk = TFS_BLOCK_SIZE - boff;
        unsigned int page;
        if (chunk > count - done)
            chunk = count - done;
        if (chunk > in->size - f->offset)
            chunk = in->size - f->offset;
        page = file_block(in, bno, 0);
        if (page)
            kmemcpy((unsigned char *)buf + done, (void *)(page + boff), chunk);
        else
            kmemset((unsigned char *)buf + done, 0, chunk); /* sparse hole */
        done += chunk;
        f->offset += chunk;
    }
    in->atime = now();
    *nread = done;
    return TFS_OK;
}

int tfs_write(struct tfs_open_file *f, const void *buf, uint32_t count,
              uint32_t *nwritten)
{
    struct tfs_inode *in;
    uint32_t done = 0;

    if (!nwritten)
        return TFS_EBADF;
    *nwritten = 0;
    if (!f || !f->in_use)
        return TFS_EBADF;
    if (!buf && count)
        return TFS_EINVAL;
    if ((f->flags & TFS_O_ACCMODE) == TFS_O_RDONLY)
        return TFS_EACCES;
    in = inode_at(f->inode);
    if (!in)
        return TFS_EBADF;
    if (is_dir(in))
        return TFS_EISDIR;
    if (f->flags & TFS_O_APPEND)
        f->offset = in->size;
    if (f->offset > TFS_MAX_FILE_SIZE ||
        count > TFS_MAX_FILE_SIZE - f->offset)
        return TFS_ENOSPC;

    while (done < count) {
        unsigned int bno = f->offset / TFS_BLOCK_SIZE;
        unsigned int boff = f->offset % TFS_BLOCK_SIZE;
        unsigned int chunk = TFS_BLOCK_SIZE - boff;
        unsigned int page;
        if (chunk > count - done)
            chunk = count - done;
        page = file_block(in, bno, 1);
        if (!page) {
            *nwritten = done;
            return TFS_ENOSPC;
        }
        kmemcpy((void *)(page + boff), (const unsigned char *)buf + done, chunk);
        done += chunk;
        f->offset += chunk;
        if (f->offset > in->size)
            in->size = f->offset;
    }
    in->mtime = in->ctime = now();
    *nwritten = done;
    return TFS_OK;
}

int tfs_lseek(struct tfs_open_file *f, int32_t offset, int whence)
{
    struct tfs_inode *in;
    int64_t base;
    int64_t target;

    if (!f || !f->in_use)
        return TFS_EBADF;
    in = inode_at(f->inode);
    if (!in)
        return TFS_EBADF;
    if (is_dir(in))
        return TFS_EISDIR;
    if (whence == TFS_SEEK_SET)
        base = 0;
    else if (whence == TFS_SEEK_CUR)
        base = f->offset;
    else if (whence == TFS_SEEK_END)
        base = in->size;
    else
        return TFS_EINVAL;
    target = base + offset;
    if (target < 0 || target > TFS_MAX_FILE_SIZE)
        return TFS_EINVAL;
    f->offset = (uint32_t)target;
    return (int)f->offset;
}

int tfs_readdir(struct tfs_open_file *f, void *buf, uint32_t bufsize,
                uint32_t *nread)
{
    struct tfs_inode *in;
    unsigned int bno, boff, need;
    struct tfs_dirent *d;

    if (!nread)
        return TFS_EBADF;
    *nread = 0;
    if (!f || !f->in_use || !buf)
        return TFS_EBADF;
    if (bufsize < 8u)
        return TFS_EINVAL;
    in = inode_at(f->inode);
    if (!in || !is_dir(in))
        return TFS_ENOTDIR;

    for (;;) {
        if (f->offset >= in->size) {
            *nread = 0;
            return TFS_OK;
        }
        if (f->offset & 3u)
            return TFS_EINVAL;
        bno = f->offset / TFS_BLOCK_SIZE;
        boff = f->offset % TFS_BLOCK_SIZE;
        if (boff + 8u > TFS_BLOCK_SIZE) {
            f->offset = (bno + 1u) * TFS_BLOCK_SIZE;
            continue;
        }
        if (bno >= TFS_DIRECT_BLOCKS || !in->blocks[bno])
            return TFS_EIO;
        d = (struct tfs_dirent *)(in->blocks[bno] + boff);
        if (d->reclen < 8u) {
            if (boff) {
                f->offset = (bno + 1u) * TFS_BLOCK_SIZE;
                continue;
            }
            return TFS_EIO;
        }
        if ((d->reclen & 3u) || d->reclen > TFS_BLOCK_SIZE - boff ||
            f->offset + d->reclen > in->size)
            return TFS_EIO;
        need = 8u + d->namelen;
        if (need < 8u || need > d->reclen || need > bufsize)
            return TFS_EINVAL;
        if (d->inode >= sb->max_inodes)
            return TFS_EIO;
        {
            struct tfs_inode *target = inode_at(d->inode);
            if (!target || (target->mode & TFS_IFMT) >> 12 != d->type)
                return TFS_EIO;
        }
        kmemcpy(buf, d, need);
        f->offset += d->reclen;
        *nread = need;
        return TFS_OK;
    }
}

int tfs_mkdir(const char *path, uint32_t mode)
{
    const char *leaf = 0;
    unsigned int leaf_len = 0;
    uint32_t parent = 0;
    struct tfs_inode *pdir;
    struct tfs_inode *dir;
    uint32_t ino;
    int rc;

    if (!mounted)
        return TFS_EINVAL;
    rc = resolve(path, &leaf, &leaf_len, &parent);
    if (rc >= 0)
        return TFS_EEXIST;
    if (rc != TFS_ENOENT || !leaf)
        return rc;
    pdir = inode_at(parent);
    if (!pdir || !is_dir(pdir))
        return TFS_ENOTDIR;
    if (!allowed(pdir, 2))
        return TFS_EACCES;
    if (dir_lookup(pdir, leaf, leaf_len) >= 0)
        return TFS_EEXIST;
    if (alloc_inode(TFS_IFDIR | (mode & 0777u), parent, &ino) != TFS_OK)
        return TFS_ENOSPC;
    rc = dir_add(pdir, ino, leaf, leaf_len, TFS_IFDIR >> 12);
    if (rc != TFS_OK) {
        dir = inode_at(ino);
        if (dir) {
            dir->mode = 0;
            sb->used_inodes--;
        }
        return rc;
    }
    pdir->nlink++;
    return TFS_OK;
}

int tfs_stat(const char *path, struct tfs_stat *st)
{
    int ino;
    struct tfs_inode *in;

    if (!mounted || !st)
        return TFS_EINVAL;
    ino = resolve(path, 0, 0, 0);
    if (ino < 0)
        return ino;
    in = inode_at((uint32_t)ino);
    if (!in)
        return TFS_ENOENT;
    st->ino = (uint32_t)ino;
    st->mode = in->mode;
    st->uid = in->uid;
    st->gid = in->gid;
    st->size = in->size;
    st->nlink = in->nlink;
    st->parent = in->parent;
    return TFS_OK;
}

void tfs_init_process_fds(void *open_files_ptr)
{
    struct tfs_open_file *fds = (struct tfs_open_file *)open_files_ptr;
    unsigned int i;
    if (!fds)
        return;
    for (i = 0; i < TFS_MAX_OPEN; i++) {
        fds[i].in_use = 0;
        fds[i].inode = 0;
        fds[i].offset = 0;
        fds[i].flags = 0;
    }
}

/*
 * Boot-time self-test: exercise mkdir -> open/write/close -> open/read/close
 * -> readdir -> stat and read back the stored bytes.  The fixture lives under
 * /tmp so the standard root remains clean.  Returns 0 on success or a negative
 * TFS_* error identifying the first failure.  Runs in kernel mode with a
 * scratch open-file, independent of the syscall layer.
 */
int tfs_selftest(void)
{
    struct tfs_open_file f;
    struct tfs_stat st;
    char buf[32];
    uint32_t n;
    int rc;

    if (!mounted)
        return TFS_EINVAL;
    rc = tfs_mkdir("/tmp/.tfs-selftest", 0755u);
    if (rc != TFS_OK)
        return rc;

    rc = tfs_open("/tmp/.tfs-selftest/hello", TFS_O_WRONLY | TFS_O_CREAT | TFS_O_TRUNC, &f);
    if (rc != TFS_OK)
        return rc;
    rc = tfs_write(&f, "hello tfs", 9, &n);
    if (rc != TFS_OK || n != 9)
        return TFS_EIO;
    tfs_close(&f);

    rc = tfs_open("/tmp/.tfs-selftest/hello", TFS_O_RDONLY, &f);
    if (rc != TFS_OK)
        return rc;
    rc = tfs_read(&f, buf, 9, &n);
    if (rc != TFS_OK || n != 9)
        return TFS_EIO;
    if (kmemcmp(buf, "hello tfs", 9) != 0)
        return TFS_EIO;
    tfs_close(&f);

    /* readdir the new directory: must contain "hello". */
    rc = tfs_open("/tmp/.tfs-selftest", TFS_O_RDONLY, &f);
    if (rc != TFS_OK)
        return rc;
    {
        int found = 0;
        for (;;) {
            struct tfs_dirent *d = (struct tfs_dirent *)buf;
            rc = tfs_readdir(&f, buf, sizeof(buf), &n);
            if (rc != TFS_OK)
                return rc;
            if (n == 0)
                break;
            if (d->namelen == 5 && kmemcmp(d->name, "hello", 5) == 0)
                found = 1;
        }
        tfs_close(&f);
        if (!found)
            return TFS_EIO;
    }

    rc = tfs_stat("/tmp/.tfs-selftest/hello", &st);
    if (rc != TFS_OK)
        return rc;
    if (st.size != 9 || (st.mode & TFS_IFMT) != TFS_IFREG)
        return TFS_EIO;
    rc = tfs_stat("/tmp/../tmp/.tfs-selftest/hello", &st);
    if (rc != TFS_OK || st.size != 9)
        return TFS_EIO;
    rc = tfs_stat("/tmp/.tfs-selftest/hello/", &st);
    if (rc != TFS_ENOTDIR)
        return TFS_EIO;

    /* Cross a direct-block boundary and exercise lseek/read-back. */
    {
        unsigned int i;
        unsigned int total = sizeof(selftest_buf);
        for (i = 0; i < total; i++)
            selftest_buf[i] = (unsigned char)(i * 31u + 7u);
        rc = tfs_open("/tmp/.tfs-selftest/blob",
                      TFS_O_WRONLY | TFS_O_CREAT | TFS_O_TRUNC, &f);
        if (rc != TFS_OK)
            return rc;
        rc = tfs_write(&f, selftest_buf, total, &n);
        if (rc != TFS_OK || n != total) {
            tfs_close(&f);
            return TFS_EIO;
        }
        tfs_close(&f);

        rc = tfs_open("/tmp/.tfs-selftest/blob", TFS_O_RDONLY, &f);
        if (rc != TFS_OK)
            return rc;
        rc = tfs_read(&f, selftest_buf, total, &n);
        if (rc != TFS_OK || n != total) {
            tfs_close(&f);
            return TFS_EIO;
        }
        for (i = 0; i < total; i++) {
            if (selftest_buf[i] != (unsigned char)(i * 31u + 7u)) {
                tfs_close(&f);
                return TFS_EIO;
            }
        }
        rc = tfs_lseek(&f, 17, TFS_SEEK_SET);
        if (rc != 17) {
            tfs_close(&f);
            return rc;
        }
        rc = tfs_read(&f, selftest_buf, 20, &n);
        if (rc != TFS_OK || n != 20) {
            tfs_close(&f);
            return TFS_EIO;
        }
        for (i = 0; i < 20; i++) {
            if (selftest_buf[i] != (unsigned char)((i + 17u) * 31u + 7u)) {
                tfs_close(&f);
                return TFS_EIO;
            }
        }
        tfs_close(&f);
    }

    /* Force a directory record across a page tail; readers must skip padding. */
    {
        static const char dir_path[] = "/tmp/.tfs-dirtest";
        char path[256];
        unsigned int i, j, pos, count;

        rc = tfs_mkdir(dir_path, 0755u);
        if (rc != TFS_OK)
            return rc;
        for (i = 0; i < 22u; i++) {
            pos = 0;
            for (j = 0; dir_path[j]; j++)
                path[pos++] = dir_path[j];
            path[pos++] = '/';
            for (j = 0; j < 190u; j++)
                path[pos++] = (char)('a' + i);
            path[pos] = '\0';
            rc = tfs_open(path, TFS_O_WRONLY | TFS_O_CREAT | TFS_O_TRUNC, &f);
            if (rc != TFS_OK)
                return rc;
            tfs_close(&f);
        }

        rc = tfs_open(dir_path, TFS_O_RDONLY, &f);
        if (rc != TFS_OK)
            return rc;
        count = 0;
        for (;;) {
            rc = tfs_readdir(&f, selftest_buf, sizeof(selftest_buf), &n);
            if (rc != TFS_OK) {
                tfs_close(&f);
                return rc;
            }
            if (n == 0)
                break;
            count++;
        }
        tfs_close(&f);
        if (count != 22u)
            return TFS_EIO;
    }

    return 0;
}
