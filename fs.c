/*
Filesystem Lab disigned and implemented by Liang Junkai,RUC
*/

#include "disk.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <fuse/fuse.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#define DIRMODE (S_IFDIR | 0755)
#define REGMODE (S_IFREG | 0644)

#define ceil_div(a, b) (((a) + (b) - 1) / (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

struct superblock {
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t inode_num;
    uint32_t block_num;
    uint32_t inode_bitmap_block;
    uint32_t block_bitmap_block;
    uint32_t inode_block;
    uint32_t data_block;
};

#define DIRECT_BLOCK_NUM 12
// #define SINGLE_INDIRECT_BLOCK_NUM 1

struct inode {
    uint32_t mode;
    uint32_t size;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t block_point[DIRECT_BLOCK_NUM];
    // uint32_t block_point_indirect[SINGLE_INDIRECT_BLOCK_NUM];
};

#define INODE_SIZE 128
#define INODE_NUM 1024
#define SUPERBLOCK_BLOCK 0
#define BITMAP_BLOCK_INODE 1
#define BITMAP_BLOCK_DATA 2
#define INODE_TABLE_START 3
#define DATA_BLOCK_START 35
#define DATA_BLOCK_SIZE (BLOCK_NUM - DATA_BLOCK_START)

#define MIN_AVAILABLE_SIZE (250 * 1024 * 1024)
#define MIN_FILE_SIZE 32768

#define MAX_FILENAME_LEN 25
#define DIR_ENTRY_SIZE 32
#define DIR_ENTRY_NUM (BLOCK_SIZE / DIR_ENTRY_SIZE)

#define ROOT_INODE 0

struct dir_entry {
    char name[MAX_FILENAME_LEN];
    uint32_t inode_pos;
};

int fs_mkdir(const char* path, mode_t mode);

// Bit operations for the bitmap
void set_bit(char* buf, int pos)
{
    buf[pos / 8] |= 1 << (pos % 8);
}
void clear_bit(char* buf, int pos)
{
    buf[pos / 8] &= ~(1 << (pos % 8));
}
bool get_bit(char* buf, int pos)
{
    return (buf[pos / 8] >> (pos % 8)) & 1;
}
int find_empty_bit(char* buf, int size)
{
    for (int i = 0; i < size; i++) {
        if (buf[i / 8] & (1 << (i % 8))) {
            continue;
        }
        return i;
    }
    return -1;
}
int alloc_block(int bitmap_block, int bitmap_size)
{
    // read the block bitmap
    char block_bitmap[BLOCK_SIZE];
    if (disk_read(bitmap_block, block_bitmap)) {
        return -1;
    }

    // find an empty block
    int block_pos = find_empty_bit(block_bitmap, bitmap_size);
    if (block_pos == -1) {
        return -1;
    }

    // set the block bitmap
    set_bit(block_bitmap, block_pos);
    if (disk_write(bitmap_block, block_bitmap)) {
        return -1;
    }

    return block_pos;
}
int clear_block(int bitmap_block, int block_pos)
{
    // read the block bitmap
    char block_bitmap[BLOCK_SIZE];
    if (disk_read(bitmap_block, block_bitmap)) {
        return -1;
    }

    // clear the block bitmap
    clear_bit(block_bitmap, block_pos);
    if (disk_write(bitmap_block, block_bitmap)) {
        return -1;
    }

    return 0;
}

// Read and write the inode
int inode_read(int inode_pos, struct inode* inode)
{
    printf("Inode_read is called:%d\n", inode_pos);
    int inode_block = INODE_TABLE_START + inode_pos * INODE_SIZE / BLOCK_SIZE;
    int inode_offset = inode_pos * INODE_SIZE % BLOCK_SIZE;
    printf("read inode_block:%d,inode_offset:%d\n", inode_block, inode_offset);

    char buf[BLOCK_SIZE];
    if (disk_read(inode_block, buf)) {
        return -1;
    }

    memcpy(inode, buf + inode_offset, sizeof(struct inode));
    return 0;
}
int inode_write(int inode_pos, struct inode* inode)
{
    printf("Inode_write is called:%d\n", inode_pos);
    int inode_block = INODE_TABLE_START + inode_pos * INODE_SIZE / BLOCK_SIZE;
    int inode_offset = inode_pos * INODE_SIZE % BLOCK_SIZE;
    printf("write inode_block:%d,inode_offset:%d\n", inode_block, inode_offset);
    char buf[BLOCK_SIZE];
    if (disk_read(inode_block, buf)) {
        return -1;
    }
    memcpy(buf + inode_offset, inode, sizeof(struct inode));
    if (disk_write(inode_block, buf)) {
        return -1;
    }
    return 0;
}
int update_inode(int inode_pos)
{
    struct inode inode;
    if (inode_read(inode_pos, &inode)) {
        return -1;
    }
    inode.ctime = time(NULL);
    if (inode_write(inode_pos, &inode)) {
        return -1;
    }
    return 0;
}

// Read and write the data block
int data_read(int block_pos, char* buf)
{
    if (disk_read(DATA_BLOCK_START + block_pos, buf)) {
        return -1;
    }
    return 0;
}
int data_write(int block_pos, char* buf)
{
    if (disk_write(DATA_BLOCK_START + block_pos, buf)) {
        return -1;
    }
    return 0;
}

struct dir_entry* add_dir_entry(int dir_inode, const struct dir_entry* entry)
{
    // read the inode
    struct inode inode;
    if (inode_read(dir_inode, &inode)) {
        return NULL;
    }

    // get the data blocks and find an empty entry
    char buf[BLOCK_SIZE];
    for (int i = 0; i < DIRECT_BLOCK_NUM; i++) { // TODO: get current block number by size
        if (inode.block_point[i] == -1) {
            inode.block_point[i] = alloc_block(BITMAP_BLOCK_DATA, DATA_BLOCK_SIZE);
            if (inode_write(dir_inode, &inode)) {
                return NULL;
            }
        }
        if (data_read(inode.block_point[i], buf)) {
            return NULL;
        }
        for (int j = 0; j < DIR_ENTRY_NUM; j++) {
            struct dir_entry* e = (struct dir_entry*)(buf + j * DIR_ENTRY_SIZE);
            if (e->inode_pos == 0) {
                memcpy(e, entry, sizeof(struct dir_entry));
                if (data_write(inode.block_point[i], buf)) {
                    return NULL;
                }
                return e;
            }
        }
    }
    return NULL;
}
struct dir_entry* find_dir_entry(int dir_inode, const char* entry_name)
{
    // read the inode
    struct inode inode;
    if (inode_read(dir_inode, &inode)) {
        return NULL;
    }

    // get the data blocks and find the entry
    char buf[BLOCK_SIZE];
    for (int i = 0; i < DIRECT_BLOCK_NUM; i++) {
        if (inode.block_point[i] == -1) {
            continue;
        }
        if (data_read(inode.block_point[i], buf)) {
            return NULL;
        }
        for (int j = 0; j < DIR_ENTRY_NUM; j++) {
            struct dir_entry* entry = (struct dir_entry*)(buf + j * DIR_ENTRY_SIZE);
            if (entry->inode_pos == 0) {
                continue;
            }
            if (strcmp(entry->name, entry_name) == 0) {
                return entry;
            }
        }
    }
    return NULL;
}
int remove_dir_entry(int dir_inode, const char* entry_name, struct dir_entry* old_entry)
{
    // read the inode
    struct inode inode;
    if (inode_read(dir_inode, &inode)) {
        return -1;
    }

    // get the data blocks and find the entry
    char buf[BLOCK_SIZE];
    for (int i = 0; i < DIRECT_BLOCK_NUM; i++) {
        if (inode.block_point[i] == -1) {
            continue;
        }
        if (data_read(inode.block_point[i], buf)) {
            return -1;
        }
        for (int j = 0; j < DIR_ENTRY_NUM; j++) {
            struct dir_entry* entry = (struct dir_entry*)(buf + j * DIR_ENTRY_SIZE);
            if (entry->inode_pos == 0) {
                continue;
            }
            if (strcmp(entry->name, entry_name) == 0) {
                if (old_entry) {
                    *old_entry = *entry;
                }
                entry->inode_pos = 0;
                if (data_write(inode.block_point[i], buf)) {
                    return -1;
                }
                return 0;
            }
        }
    }
    return -1;
}

// Called every time a directory entry is visited. Return 0 to continue, nonzero to break
typedef int (*walk_dir_entry_callback)(struct dir_entry*, void* context);
int walk_dir_entry(int dir_inode, walk_dir_entry_callback callback, void* context)
{
    // read the inode
    struct inode inode;
    if (inode_read(dir_inode, &inode)) {
        return -1;
    }

    // get the data blocks and find the entry
    char buf[BLOCK_SIZE];
    for (int i = 0; i < DIRECT_BLOCK_NUM; i++) {
        if (inode.block_point[i] == -1) {
            continue;
        }
        if (data_read(inode.block_point[i], buf)) {
            return -1;
        }
        for (int j = 0; j < DIR_ENTRY_NUM; j++) {
            struct dir_entry* entry = (struct dir_entry*)(buf + j * DIR_ENTRY_SIZE);
            if (entry->inode_pos == 0) {
                continue;
            }
            if (callback(entry, context)) {
                return 0;
            }
        }
    }
    return 0;
}

#define MAX_LAYER 10

// Resolve the path to the inode
// Return the inode position if the path exists, -1 otherwise
int resolve_path_to_inode(const char* path, struct inode* inode)
{
    char path_layer[MAX_LAYER][MAX_FILENAME_LEN];
    int layer_count = 0;
    char* path4dir = strdup(path);
    char* dir = path4dir;
    for (; strcmp(dir, "/") != 0 && strcmp(dir, ".") != 0; layer_count++, dir = dirname(dir)) {
        const char* base = basename(dir);
        assert(layer_count < MAX_LAYER);
        strncpy(path_layer[layer_count], base, MAX_FILENAME_LEN);
        printf("path_layer[%d]:%s\n", layer_count, path_layer[layer_count]);
    }
    free(path4dir);

    int inode_pos = ROOT_INODE;
    while (layer_count-- > 0) {
        struct dir_entry* entry = find_dir_entry(inode_pos, path_layer[layer_count]);
        if (entry == NULL) {
            return -1;
        }
        inode_pos = entry->inode_pos;
        printf("inode_pos:%d\n", inode_pos);
    }

    if (inode && inode_read(inode_pos, inode)) {
        return -1;
    }
    return inode_pos;
}

// Format the virtual block device: basic filesystem structure, root directory, etc.
// Return 0 if the operation is successful, not 0 otherwise
int mkfs()
{
    printf("Mkfs is called\n");

    // clear the disk
    char buf[BLOCK_SIZE] = { 0 };
    for (int i = 0; i < BLOCK_NUM; i++) {
        if (disk_write(i, buf)) {
            return -1;
        }
    }

    static_assert(sizeof(struct inode) <= INODE_SIZE, "The inode should be smaller than INODE_SIZE");
    static_assert(sizeof(struct superblock) <= BLOCK_SIZE, "The superblock should be smaller than BLOCK_SIZE");
    static_assert(INODE_SIZE * INODE_NUM <= BLOCK_SIZE * (DATA_BLOCK_START - INODE_TABLE_START), "The inode table should be smaller than assigned blocks");
    static_assert(BLOCK_SIZE % INODE_SIZE == 0, "The inode should be aligned with the block size");

    static_assert((BLOCK_NUM - DATA_BLOCK_START) * BLOCK_SIZE >= MIN_AVAILABLE_SIZE, "The available size should be larger than MIN_AVAILABLE_SIZE");

    static_assert(sizeof(struct dir_entry) <= DIR_ENTRY_SIZE, "The directory entry should be smaller than DIR_ENTRY_SIZE");
    static_assert(DIR_ENTRY_SIZE * DIR_ENTRY_NUM <= BLOCK_SIZE, "The directory should be smaller than BLOCK_SIZE");
    static_assert(BLOCK_SIZE % DIR_ENTRY_SIZE == 0, "The directory should be aligned with the block size");

    // write the superblock
    struct superblock sb = {
        .block_size = BLOCK_SIZE,
        .inode_size = INODE_SIZE,
        .inode_num = INODE_NUM,
        .inode_bitmap_block = BITMAP_BLOCK_INODE,
        .block_bitmap_block = BITMAP_BLOCK_DATA,
        .inode_block = INODE_TABLE_START,
        .data_block = DATA_BLOCK_START,
    };
    if (disk_write(SUPERBLOCK_BLOCK, &sb)) {
        return -1;
    }

    // write the root directory
    struct inode root_inode = {
        .mode = DIRMODE,
        .size = DIR_ENTRY_SIZE,
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
    };
    memset(root_inode.block_point, -1, sizeof(root_inode.block_point));
    if (inode_write(ROOT_INODE, &root_inode)) {
        return -1;
    }
    set_bit(buf, 0);
    if (disk_write(BITMAP_BLOCK_INODE, buf)) {
        return -1;
    }

    return 0;
}

int getattr(int inode_pos, struct stat* attr)
{
    struct inode inode;
    if (inode_read(inode_pos, &inode)) {
        return 1;
    }
    *attr = (struct stat) {
        .st_mode = inode.mode,
        .st_nlink = 1,
        .st_uid = getuid(),
        .st_gid = getgid(),
        .st_size = inode.size,
        .st_atime = inode.atime,
        .st_mtime = inode.mtime,
        .st_ctime = inode.ctime,
    };
    return 0;
}

// Query the attributes of a regular file or directory
// Return -ENOENT if the file or directory does not exist
int fs_getattr(const char* path, struct stat* attr)
{
    printf("Getattr is called:%s\n", path);

    int inode_pos = resolve_path_to_inode(path, NULL);
    if (inode_pos == -1) {
        return -ENOENT;
    }

    return getattr(inode_pos, attr);
}

struct fill_dir_context {
    fuse_fill_dir_t filler;
    void* buf;
};
int readaddr_callback(struct dir_entry* entry, void* context)
{
    struct fill_dir_context* ctx = (struct fill_dir_context*)context;
    struct stat st;
    if (getattr(entry->inode_pos, &st)) {
        return -1;
    }
    ctx->filler(ctx->buf, entry->name, &st, 0);
    return 0;
}

// Read all entries in a directory
// Update the `atime` of the directory
// `ls` command can trigger this function
int fs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, [[maybe_unused]] off_t offset, struct fuse_file_info* fi)
{
    printf("Readdir is called:%s\n", path);

    // read the inode
    struct inode inode;
    int inode_pos = resolve_path_to_inode(path, &inode);
    if (inode_pos == -1) {
        return -ENOENT;
    }

    struct fill_dir_context context = {
        .filler = filler,
        .buf = buffer,
    };
    if (walk_dir_entry(inode_pos, readaddr_callback, &context)) {
        return -1;
    }

    // update the inode
    inode.atime = time(NULL);
    if (inode_write(inode_pos, &inode)) {
        return -1;
    }
    return 0;
}

// Read the contents of a regular file
// Update the `atime` of the file
// `cat` command can trigger this function
// Return the number of bytes read
int fs_read(const char* path, char* buffer, size_t size, off_t offset, struct fuse_file_info* fi)
{
    printf("Read is called:%s\n", path);

    int inode_pos = fi->fh;

    struct inode inode;
    if (inode_read(inode_pos, &inode)) {
        return -1;
    }

    if (offset >= inode.size) {
        return 0;
    }

    size = min(size, inode.size - offset);
    int total_read = 0;

    char buf[BLOCK_SIZE];
    while (size > 0) {
        int block_idx = (offset + total_read) / BLOCK_SIZE;
        int block_offset = (offset + total_read) % BLOCK_SIZE;
        int read_from_block = min(size, BLOCK_SIZE - block_offset);

        if (inode.block_point[block_idx] == -1) {
            break; // No more data blocks to read
        }

        if (data_read(inode.block_point[block_idx], buf) == -1) {
            return -1;
        }

        memcpy(buffer + total_read, buf + block_offset, read_from_block);
        total_read += read_from_block;
        size -= read_from_block;
    }

    inode.atime = time(NULL);
    if (inode_write(inode_pos, &inode)) {
        return -1;
    }
    return total_read;
}

// Create a regular file or directory, depending on the `mode`
// Update the `ctime` and `mtime` of the parent directory
// Return -ENOSPC if no enough space or file nodes
int make_file(const char* path, mode_t mode)
{
    // allocate an inode
    int inode_pos = alloc_block(BITMAP_BLOCK_INODE, INODE_NUM);
    if (inode_pos == -1) {
        return -ENOSPC;
    }

    // write the inode
    struct inode inode = {
        .mode = mode,
        .size = 0,
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
    };
    memset(inode.block_point, -1, sizeof(inode.block_point));
    if (inode_write(inode_pos, &inode)) {
        return -1;
    }

    // write the parent directory inode
    char* path4dir = strdup(path);
    char* dir = dirname(path4dir);
    int parent_inode = resolve_path_to_inode(dir, &inode);
    if (parent_inode == -1) {
        return -ENOENT;
    }
    free(path4dir);

    if (inode_read(parent_inode, &inode)) {
        return -1;
    }
    inode.atime = inode.mtime = inode.ctime = time(NULL);
    inode.size += DIR_ENTRY_SIZE;
    if (inode_write(parent_inode, &inode)) {
        return -1;
    }

    struct dir_entry entry = {
        .inode_pos = inode_pos,
    };
    char* path4base = strdup(path);
    char* base = basename(strdup(path));
    strncpy(entry.name, base, MAX_FILENAME_LEN);
    free(path4base);

    // add the directory entry
    if (add_dir_entry(parent_inode, &entry) == NULL) {
        return -1;
    }
    return 0;
}

// Create a regular file
// `touch` and `echo x > file` commands can trigger this function
// Update the `ctime` and `mtime` of the parent directory
// Return -ENOSPC if no enough space or file nodes
int fs_mknod(const char* path, [[maybe_unused]] mode_t mode, [[maybe_unused]] dev_t dev)
{
    printf("Mknod is called:%s\n", path);
    return make_file(path, REGMODE);
}

// Create a directory
// `mkdir` command can trigger this function
// Update the `ctime` and `mtime` of the parent directory
// Return -ENOSPC if no enough space or file nodes
int fs_mkdir(const char* path, [[maybe_unused]] mode_t mode)
{
    printf("Mkdir is called:%s\n", path);
    return make_file(path, DIRMODE);
}

// Remove a directory entry by path
// Update the `mtime` and `ctime` of the parent directory
// Return the inode position of file specified by path, or -ENOENT if the file does not exist, or -1 on error
int remove_path_dir_entry(const char* path)
{
    char* path4dir = strdup(path);
    char* dir = dirname(path4dir);
    struct inode inode;
    int parent_inode = resolve_path_to_inode(dir, &inode);
    if (parent_inode == -1) {
        return -ENOENT;
    }
    free(path4dir);

    char* path4base = strdup(path);
    char* base = basename(path4base);
    struct dir_entry entry;
    if (remove_dir_entry(parent_inode, base, &entry)) {
        return -1;
    }
    free(path4base);

    // update the parent directory inode
    inode.atime = inode.mtime = inode.ctime = time(NULL);
    inode.size -= DIR_ENTRY_SIZE;
    if (inode_write(parent_inode, &inode)) {
        return -1;
    }

    return entry.inode_pos;
}

int remove_file(const char* path)
{
    int old_inode_pos = remove_path_dir_entry(path);
    struct inode inode;
    if (inode_read(old_inode_pos, &inode)) {
        return -1;
    }
    for (int i = 0; i < DIRECT_BLOCK_NUM; i++) {
        if (inode.block_point[i] != -1) {
            clear_block(BITMAP_BLOCK_DATA, inode.block_point[i]);
        }
    }
    clear_block(BITMAP_BLOCK_INODE, old_inode_pos);

    return 0;
}

// Remove a directory
// The directory must be empty
// `rm -r` command can trigger this function
// Update the `mtime` and `ctime` of the parent directory
int fs_rmdir(const char* path)
{
    printf("Rmdir is called:%s\n", path);
    return remove_file(path);
}

// Remove a regular file
// `rm` command can trigger this function
// Update the `mtime` and `ctime` of the parent directory
int fs_unlink(const char* path)
{
    printf("Unlink is callded:%s\n", path);
    return remove_file(path);
}

// Change the name or location of a file or directory
// `mv` command can trigger this function
int fs_rename(const char* oldpath, const char* newpath)
{
    printf("Rename is called:%s\n", newpath);

    // first remove the old entry
    int inode_pos = remove_path_dir_entry(oldpath);
    if (inode_pos < 0) {
        return inode_pos;
    }

    // then add the new entry
    char* newpath4dir = strdup(newpath);
    char* new_dir = dirname(newpath4dir);
    int new_dir_inode = resolve_path_to_inode(new_dir, NULL);
    if (new_dir_inode == -1) {
        return -ENOENT;
    }
    free(newpath4dir);

    struct dir_entry new_entry = {
        .inode_pos = inode_pos,
    };
    char* newpath4base = strdup(newpath);
    char* new_base = basename(strdup(newpath4base));
    strncpy(new_entry.name, new_base, MAX_FILENAME_LEN);
    free(newpath4base);

    if (add_dir_entry(new_dir_inode, &new_entry) == NULL) {
        return -1;
    }
    return 0;
}

int inode_truncate(struct inode* inode, off_t size)
{
    if (size > BLOCK_SIZE * DIRECT_BLOCK_NUM) {
        return -ENOSPC;
    }

    inode->atime = inode->ctime = time(NULL);
    if (inode->size < size) {
        // allocate the data blocks
        for (int i = ceil_div(inode->size, BLOCK_SIZE); i < ceil_div(size, BLOCK_SIZE); i++) {
            int block_pos = alloc_block(BITMAP_BLOCK_DATA, DATA_BLOCK_SIZE);
            if (block_pos == -1) {
                return -ENOSPC;
            }
            inode->block_point[i] = block_pos;
        }
    } else if (inode->size > size) {
        // release the data blocks
        for (int i = ceil_div(size, BLOCK_SIZE); i < ceil_div(inode->size, BLOCK_SIZE); i++) {
            clear_block(BITMAP_BLOCK_DATA, inode->block_point[i]);
        }
    }
    inode->size = size;

    return 0;
}

// Write data to a regular file
// Update the `mtime` and `ctime` of the file
// Return the number of bytes written which should be equal to `size`, or 0 on error
int fs_write(const char* path, const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi)
{
    printf("Write is called:%s\n", path);

    int inode_pos = fi->fh;

    struct inode inode;
    if (inode_read(inode_pos, &inode)) {
        return -1;
    }

    if (fi->flags & O_APPEND) {
        // the system should set the file offset to the end of the file when `O_APPEND` is set
        // but we explicitly set again to prevent some unexpected behavior
        offset = inode.size;
    }

    // Adjust the size of the file first
    if (offset + size > inode.size) {
        if (inode_truncate(&inode, offset + size)) {
            return 0;
        }
    }

    int total_written = 0;
    char buf[BLOCK_SIZE];
    while (size > 0) {
        int block_idx = (offset + total_written) / BLOCK_SIZE;
        int block_offset = (offset + total_written) % BLOCK_SIZE;
        int write_to_block = min(size, BLOCK_SIZE - block_offset);

        assert(inode.block_point[block_idx] != -1); // The block should be allocated
        if (data_read(inode.block_point[block_idx], buf) == -1) {
            return 0;
        }
        memcpy(buf + block_offset, buffer + total_written, write_to_block);
        if (data_write(inode.block_point[block_idx], buf) == -1) {
            return 0;
        }

        total_written += write_to_block;
        size -= write_to_block;
    }

    inode.mtime = inode.ctime = time(NULL);
    if (inode_write(inode_pos, &inode)) {
        return 0;
    }

    return total_written;
}

// Change the size of a regular file
// `truncate` command can trigger this function
// Update the `ctime` of the file
// Return -ENOSPC if no enough space
int fs_truncate(const char* path, off_t size)
{
    printf("Truncate is called:%s\n", path);

    struct inode inode;
    int inode_pos = resolve_path_to_inode(path, &inode);
    assert(inode.mode == REGMODE);

    if (inode_truncate(&inode, size)) {
        return -ENOSPC;
    }

    if (inode_write(inode_pos, &inode)) {
        return -1;
    }
    return 0;
}

// Change the access and modification times of a regular file or directory
// Update the `ctime` of the file
int fs_utime(const char* path, struct utimbuf* buffer)
{
    printf("Utime is called:%s\n", path);

    struct inode inode;
    int inode_pos = resolve_path_to_inode(path, &inode);

    inode.atime = buffer->actime;
    inode.mtime = buffer->modtime;
    inode.ctime = time(NULL);
    if (inode_write(inode_pos, &inode)) {
        return -1;
    }

    return 0;
}

// Get file system statistics
// `df` command can trigger this function
int fs_statfs([[maybe_unused]] const char* path, struct statvfs* stat)
{
    printf("Statfs is called:%s\n", path);

    // f_bfree == f_bavail, f_ffree == f_favail
    *stat = (struct statvfs) {
        .f_bsize = 0,
        .f_blocks = 0,
        .f_bfree = 0,
        .f_bavail = 0,
        .f_files = 0,
        .f_ffree = 0,
        .f_favail = 0,
        .f_namemax = 0,
    };

    return 0;
}

// Open a regular file
// `fi->fh` is a reserved field for file handle
// `fi->flags` contains the flags. Where `O_APPEND` requires special handling and is tested with `echo x >> file`.
int fs_open(const char* path, struct fuse_file_info* fi)
{
    printf("Open is called:%s\n", path);

    // if the file does not exist, create it
    if (fi->flags & O_CREAT) {
        fs_mknod(path, 0, 0);
    }

    struct inode inode;
    int inode_pos = resolve_path_to_inode(path, &inode);
    if (inode_pos == -1) {
        return -ENOENT;
    }

    fi->fh = inode_pos;
    return 0;
}

#pragma region fixed

// Release an opened regular file
int fs_release(const char* path, struct fuse_file_info* fi)
{
    printf("Release is called:%s\n", path);
    return 0;
}

// Open a directory
int fs_opendir(const char* path, struct fuse_file_info* fi)
{
    printf("Opendir is called:%s\n", path);
    return 0;
}

// Release an opened directory
int fs_releasedir(const char* path, struct fuse_file_info* fi)
{
    printf("Releasedir is called:%s\n", path);
    return 0;
}

static struct fuse_operations fs_operations = {
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .read = fs_read,
    .mkdir = fs_mkdir,
    .rmdir = fs_rmdir,
    .unlink = fs_unlink,
    .rename = fs_rename,
    .truncate = fs_truncate,
    .utime = fs_utime,
    .mknod = fs_mknod,
    .write = fs_write,
    .statfs = fs_statfs,
    .open = fs_open,
    .release = fs_release,
    .opendir = fs_opendir,
    .releasedir = fs_releasedir
};

int main(int argc, char* argv[])
{
    if (disk_init()) {
        printf("Can't open virtual disk!\n");
        return -1;
    }
    if (mkfs()) {
        printf("Mkfs failed!\n");
        return -2;
    }
    return fuse_main(argc, argv, &fs_operations, NULL);
}

#pragma endregion