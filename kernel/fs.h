#ifndef FS_H
#define FS_H

#include "types.h"

/*
 * TinyFS (TFS) — a small core-UNIX filesystem.
 *
 * Classic UNIX concepts, nothing more:
 *   filesystem -> directories -> directory entries -> files -> file descriptors
 *
 * On-disk layout (each "block" is one 4 KiB physical page, RAM-backed for v1):
 *   [ superblock page ]
 *   [ inode table: one inode per page ]
 *   [ data pages, allocated on demand ]
 *
 * An inode stores a mode (file type + permission bits), owner, size and a
 * small array of direct block pointers. Directories are files whose data is a
 * sequence of variable-length directory entries. Paths may be absolute or
 * relative; relative paths start at /. No journaling, no indirect blocks, no
 * copy-on-write — deliberately.
 */

#define TFS_MAGIC        0x54465331u   /* "TFS1" */
#define TFS_BLOCK_SIZE   4096u
#define TFS_DIRECT_BLOCKS 60u          /* 60 * 4 KiB = 240 KiB max file */
#define TFS_MAX_FILE_SIZE (TFS_BLOCK_SIZE * TFS_DIRECT_BLOCKS)
#define TFS_MAX_INODES   64u
#define TFS_ROOT_INODE   0u
#define TFS_MAX_OPEN     16u           /* open files per process */
#define TFS_NAME_MAX     255u          /* on-disk directory-entry limit */
#define TFS_PATH_MAX     127u          /* current syscall/shell path limit */

/* inode.mode file type (classic UNIX bits in the high nibble) */
#define TFS_IFMT   0xF000u
#define TFS_IFREG  0x8000u
#define TFS_IFDIR  0x4000u

/* permission bits (owner/group/other) */
#define TFS_IRWXU  0700u
#define TFS_IRUSR  0400u
#define TFS_ISUSR  0200u
#define TFS_IWUSR  0100u
#define TFS_IRGRP  0070u
#define TFS_IROTH  0007u

/* open() flags */
#define TFS_O_RDONLY  0x0000u
#define TFS_O_WRONLY  0x0001u
#define TFS_O_RDWR    0x0002u
#define TFS_O_ACCMODE 0x0003u
#define TFS_O_CREAT   0x0100u
#define TFS_O_TRUNC   0x0200u
#define TFS_O_APPEND  0x0400u

/* lseek() whence */
#define TFS_SEEK_SET 0
#define TFS_SEEK_CUR 1
#define TFS_SEEK_END 2

/* errors (returned as negative from kernel API, surfaced to userspace) */
#define TFS_OK        0
#define TFS_ENOENT   -2
#define TFS_EEXIST   -3
#define TFS_ENOTDIR  -4
#define TFS_EISDIR   -5
#define TFS_EBADF    -6
#define TFS_EACCES   -7
#define TFS_ENOSPC   -8
#define TFS_EINVAL   -9
#define TFS_EMFILE  -10
#define TFS_ENOTEMPTY -11
#define TFS_EIO     -12
#define TFS_ENAMETOOLONG -13

/* One inode = one page. */
struct tfs_inode {
    uint32_t mode;                          /* TFS_IF* | permissions */
    uint32_t uid;
    uint32_t gid;
    uint32_t size;                          /* bytes; dirs: bytes of dirents */
    uint32_t nlink;
    uint32_t parent;                        /* parent inode (for "..") */
    uint32_t atime, mtime, ctime;
    uint32_t blocks[TFS_DIRECT_BLOCKS];      /* data page addresses, 0 = none */
    uint32_t pad[TFS_BLOCK_SIZE / 4u - 9u - TFS_DIRECT_BLOCKS];
};

struct tfs_superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t sb_page;                       /* page holding this superblock */
    uint32_t max_inodes;
    uint32_t root_inode;
    uint32_t used_inodes;                   /* allocated inodes */
    uint32_t used_blocks;                   /* allocated data pages */
    uint32_t reserved[TFS_BLOCK_SIZE / 4u - 7u];
};

typedef char tfs_inode_page_size_check[
    sizeof(struct tfs_inode) == TFS_BLOCK_SIZE ? 1 : -1];
typedef char tfs_superblock_page_size_check[
    sizeof(struct tfs_superblock) == TFS_BLOCK_SIZE ? 1 : -1];

/* Packed directory entry; reclen lets a reader skip to the next record. */
struct tfs_dirent {
    uint32_t inode;       /* inode number */
    uint16_t reclen;      /* bytes occupied by this record (4-aligned) */
    uint8_t  type;        /* TFS_IFREG>>12 or TFS_IFDIR>>12 */
    uint8_t  namelen;
    char     name[];      /* namelen bytes, not NUL-terminated */
};

/* Result of stat(); fixed 32-bit fields so ring-3 asm can read them. */
struct tfs_stat {
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t nlink;
    uint32_t parent;
};

typedef char tfs_stat_size_check[sizeof(struct tfs_stat) == 28u ? 1 : -1];

/* One open file, indexed by file descriptor within a process. */
struct tfs_open_file {
    int      in_use;
    uint32_t inode;     /* inode number */
    uint32_t offset;    /* read/write cursor; dirent cursor for directories */
    uint32_t flags;
};

/* ---- kernel API (all return TFS_OK / negative error) ---- */
int  tfs_mount(void);                        /* format and mount a fresh fs */
int  tfs_format(void);                       /* fresh fs on the current pages */

int  tfs_open(const char *path, uint32_t flags, struct tfs_open_file *out);
int  tfs_close(struct tfs_open_file *f);
int  tfs_read(struct tfs_open_file *f, void *buf, uint32_t count, uint32_t *nread);
int  tfs_write(struct tfs_open_file *f, const void *buf, uint32_t count, uint32_t *nwritten);
int  tfs_lseek(struct tfs_open_file *f, int32_t offset, int whence); /* new offset */
int  tfs_readdir(struct tfs_open_file *f, void *buf, uint32_t bufsize, uint32_t *nread);
int  tfs_mkdir(const char *path, uint32_t mode);
int  tfs_stat(const char *path, struct tfs_stat *st);

/* helpers used by the process/syscalls layer */
void tfs_init_process_fds(void *open_files_ptr);

/* Boot-time self-test; 0 on success, negative TFS_* on first failure. */
int tfs_selftest(void);

#endif
