# FSLab 报告

李知非 2022200862

## 具体实现

结构化设计

### Bitmap

`set_bit`、`clear_bit`、`get_bit`、`find_empty_bit` 函数均是以字节为单位的位操作函数。

```c
void set_bit(char* buf, int pos);
void clear_bit(char* buf, int pos);
bool get_bit(char* buf, int pos);
int find_empty_bit(char* buf, int size);
```

`alloc_block`、`clear_block` 函数则是更加 high-level 的接口函数，用于分配和释放 block 时修改 bitmap。

```c
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
```

### Data Block

Data Block 即是一块大小为 `BLOCK_SIZE` 的字节数组，用以存储文件数据。

```
#define BLOCK_SIZE 4096
```

`data_read`、`data_write` 函数是对 Data Block 读写的简单封装。

```c
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
```

### Directory Entries

Directory 作为一种特殊的文件，其内容是一系列的 `dir_entry` 结构体，每个结构体包含文件名和 inode 位置。

其中，`DIR_ENTRY_NUM` 为每个 block 中的 `dir_entry` 数量。

```c
struct dir_entry {
    char name[MAX_FILENAME_LEN];
    uint32_t inode_pos;
    // padding to `DIR_ENTRY_SIZE`
};
#define DIR_ENTRY_SIZE 32
#define DIR_ENTRY_NUM (BLOCK_SIZE / DIR_ENTRY_SIZE)
```

### Inode

Inode 结构体包含文件的元数据信息，包括文件类型、大小、时间戳、直接块指针、间接块指针等。

```c
#define DIRECT_BLOCK_NUM 12
#define SINGLE_INDIRECT_BLOCK_NUM 2

struct inode {
    uint32_t mode;
    uint32_t size;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t block_point[DIRECT_BLOCK_NUM];
    uint32_t block_point_indirect[SINGLE_INDIRECT_BLOCK_NUM];
    // padding to `INODE_SIZE`
};

#define INODE_SIZE 128
```c

我们规定，Inode 应有 `INODE_NUM` 个。

```c
#define INODE_NUM 32768
#define INODE_TABLE_SIZE (INODE_NUM * INODE_SIZE)
```

`init_inode` 函数用于初始化 Inode 结构体，`inode_read` 与 `inode_write` 函数是对 Inode 读写的简单封装。

```c
void init_inode(struct inode* inode, mode_t mode)
{
    inode->mode = mode;
    inode->size = 0;
    inode->atime = inode->mtime = inode->ctime = time(NULL);
    memset(inode->block_point, -1, sizeof(inode->block_point));
    memset(inode->block_point_indirect, -1, sizeof(inode->block_point_indirect));
}
int inode_read(int inode_pos, struct inode* inode)
{
    int inode_block = inode_pos * INODE_SIZE / BLOCK_SIZE, inode_offset = inode_pos * INODE_SIZE % BLOCK_SIZE;

    char buf[BLOCK_SIZE];
    if (disk_read(INODE_TABLE_START + inode_block, buf)) {
        return -1;
    }

    memcpy(inode, buf + inode_offset, sizeof(struct inode));
    return 0;
}
int inode_write(int inode_pos, struct inode* inode)
{
    int inode_block = inode_pos * INODE_SIZE / BLOCK_SIZE, inode_offset = inode_pos * INODE_SIZE % BLOCK_SIZE;
    char buf[BLOCK_SIZE];
    if (disk_read(INODE_TABLE_START + inode_block, buf)) {
        return -1;
    }
    memcpy(buf + inode_offset, inode, sizeof(struct inode));
    if (disk_write(INODE_TABLE_START + inode_block, buf)) {
        return -1;
    }
    return 0;
}

#### Inode 操作

`block_point_indirect` 指向的了一种特殊的 Data Block，其中存储了一系列的 `uint32_t`，每个 `uint32_t` 是 Inode 蕴含的 Data Block 的位置。

```c
#define INDIRECT_POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
#define DATA_BLOCK_PER_INODE (DIRECT_BLOCK_NUM + SINGLE_INDIRECT_BLOCK_NUM * INDIRECT_POINTERS_PER_BLOCK)
```

因此，我们创造一种 illusion，好像 Inode 的 Data Block 是平坦的，即 Inode 的 `block_id` 从 0 到 `DATA_BLOCK_PER_INODE - 1`，而通过`get_block_pos` 与 `set_block_pos` 函数，我们可以读写其真实的 Data Block 位置。

```c
// Get the real block position (block pointer) corresponding to the block_id of the inode
int get_block_pos(struct inode* inode, int id, int* block_pos)
{
    if (id < DIRECT_BLOCK_NUM) {
        *block_pos = inode->block_point[id];
        return 0;
    } else {
        int indirect_index = (id - DIRECT_BLOCK_NUM) / INDIRECT_POINTERS_PER_BLOCK, indirect_offset = (id - DIRECT_BLOCK_NUM) % INDIRECT_POINTERS_PER_BLOCK;

        if (indirect_index >= SINGLE_INDIRECT_BLOCK_NUM) {
            return -1; // Out of bounds
        }

        if (inode->block_point_indirect[indirect_index] == -1) {
            return -1; // Indirect block not allocated
        }

        uint32_t indirect_buf[INDIRECT_POINTERS_PER_BLOCK];
        if (disk_read(inode->block_point_indirect[indirect_index], (char*)indirect_buf)) {
            return -1;
        }

        *block_pos = indirect_buf[indirect_offset];
        return 0;
    }
}

// Set the real block position (block pointer) corresponding to the block_id of the inode
int set_block_pos(struct inode* inode, int id, int block_pos)
{
    if (id < DIRECT_BLOCK_NUM) {
        inode->block_point[id] = block_pos;
        return 0;
    } else {
        int indirect_index = (id - DIRECT_BLOCK_NUM) / INDIRECT_POINTERS_PER_BLOCK, indirect_offset = (id - DIRECT_BLOCK_NUM) % INDIRECT_POINTERS_PER_BLOCK;

        if (indirect_index >= SINGLE_INDIRECT_BLOCK_NUM) {
            return -1; // Out of bounds
        }

        if (inode->block_point_indirect[indirect_index] == -1) {
            if (block_pos == -1) {
                return 0; // Nothing to do, already -1
            }
            int indirect_block_pos = alloc_block(BITMAP_BLOCK_DATA, DATA_BLOCK_SIZE);
            if (indirect_block_pos == -1) {
                return -1;
            }
            inode->block_point_indirect[indirect_index] = indirect_block_pos;
            // Initialize the indirect block
            uint32_t zero_buf[INDIRECT_POINTERS_PER_BLOCK] = { 0 };
            if (disk_write(indirect_block_pos, (char*)zero_buf)) {
                return -1;
            }
        }

        uint32_t indirect_buf[INDIRECT_POINTERS_PER_BLOCK];
        if (disk_read(inode->block_point_indirect[indirect_index], (char*)indirect_buf)) {
            return -1;
        }

        indirect_buf[indirect_offset] = block_pos;
        if (disk_write(inode->block_point_indirect[indirect_index], (char*)indirect_buf)) {
            return -1;
        }

        return 0;
    }
}
```

#### Directory-Typed Inode

得益于 `get_block_pos` 与 `set_block_pos` 函数，现在 Data Blocks 是平坦的。而 Inode 蕴含的真实数据，则是这些平坦的 Data Blocks 按顺序排列所组成的。

对于 Directory-Typed Inode，其 Data Blocks 中存储了一系列的 `dir_entry` 结构体。我们可以将 Directory-Typed Inode 被抽象为一个大的 Map，包含一系列 `dir_entry` 项，只是这些项被分散存储在了 Data Blocks 中。`add_dir_entry`、`find_dir_entry`、`remove_dir_entry`、`walk_dir_entry` 函数即对应 Map 的 `insert`、`find`、`delete`、`traverse` 操作。

```c

struct dir_entry* add_dir_entry(struct inode* inode, const struct dir_entry* entry)
{
    for (int i = 0; i < DIR_ENTRY_PER_INODE; i++) {
        int block_id = i / DIR_ENTRY_NUM, block_offset = i % DIR_ENTRY_NUM;
        int block_pos;
        if (get_block_pos(inode, block_id, &block_pos)) {
            return NULL;
        }
        if (block_pos == -1) {
            block_pos = alloc_block(BITMAP_BLOCK_DATA, DATA_BLOCK_SIZE);
            if (block_pos == -1) {
                return NULL;
            }
            if (set_block_pos(inode, block_id, block_pos)) {
                return NULL;
            }
        }
        char buf[BLOCK_SIZE];
        if (data_read(block_pos, buf)) {
            return NULL;
        }
        struct dir_entry* dir_entry = (struct dir_entry*)(buf + block_offset * DIR_ENTRY_SIZE);
        if (dir_entry->inode_pos == 0) {
            *dir_entry = *entry;
            inode->size += DIR_ENTRY_SIZE;
            if (data_write(block_pos, buf)) {
                return NULL;
            }
            return dir_entry;
        }
    }
    return NULL;
}

struct dir_entry* find_dir_entry(struct inode* inode, const char* entry_name)
{
    for (int i = 0; i < DIR_ENTRY_PER_INODE; i++) {
        int block_id = i / DIR_ENTRY_NUM, block_offset = i % DIR_ENTRY_NUM;
        int block_pos;
        if (get_block_pos(inode, block_id, &block_pos)) {
            return NULL;
        }
        if (block_pos == -1) {
            continue;
        }
        char buf[BLOCK_SIZE];
        if (data_read(block_pos, buf)) {
            return NULL;
        }
        struct dir_entry* entry = (struct dir_entry*)(buf + block_offset * DIR_ENTRY_SIZE);
        if (entry->inode_pos == 0) {
            continue;
        }
        if (strcmp(entry->name, entry_name) == 0) {
            return entry;
        }
    }
    return NULL;
}
int remove_dir_entry(struct inode* inode, const char* entry_name, struct dir_entry* old_entry)
{
    for (int i = 0; i < DIR_ENTRY_PER_INODE; i++) {
        int block_id = i / DIR_ENTRY_NUM, block_offset = i % DIR_ENTRY_NUM;
        int block_pos;
        if (get_block_pos(inode, block_id, &block_pos)) {
            return -1;
        }
        if (block_pos == -1) {
            continue;
        }
        char buf[BLOCK_SIZE];
        if (data_read(block_pos, buf)) {
            return -1;
        }
        struct dir_entry* entry = (struct dir_entry*)(buf + block_offset * DIR_ENTRY_SIZE);
        if (entry->inode_pos == 0) {
            continue;
        }
        if (strcmp(entry->name, entry_name) == 0) {
            *old_entry = *entry;
            entry->inode_pos = 0;
            inode->size -= DIR_ENTRY_SIZE;
            if (data_write(block_pos, buf)) {
                return -1;
            }
            return 0;
        }
    }

    // release all unused data blocks
    for (int block_id = 0; block_id < DATA_BLOCK_PER_INODE; block_id++) {
        int block_pos;
        if (get_block_pos(inode, block_id, &block_pos)) {
            return -1;
        }
        if (block_pos == -1) {
            continue;
        }
        char buf[BLOCK_SIZE];
        if (data_read(block_pos, buf)) {
            return -1;
        }
        bool used = false;
        for (int i = 0; i < DIR_ENTRY_NUM; i++) {
            struct dir_entry* entry = (struct dir_entry*)(buf + i * DIR_ENTRY_SIZE);
            if (entry->inode_pos != 0) {
                used = true;
                break;
            }
        }
        if (!used) {
            if (clear_block(BITMAP_BLOCK_DATA, block_pos)) {
                return -1;
            }
            if (set_block_pos(inode, block_id, -1)) {
                return -1;
            }
        }
    }
    return 0;
}

// Called every time a directory entry is visited. Return 0 to continue, nonzero to break
typedef int (*walk_dir_entry_callback)(struct dir_entry*, void* context);
int walk_dir_entry(struct inode* inode, walk_dir_entry_callback callback, void* context)
{
    // get the data blocks and find the entry
    char buf[BLOCK_SIZE];
    assert(inode->size % DIR_ENTRY_SIZE == 0);
    for (int i = 0; i < DIR_ENTRY_PER_INODE; i++) {
        int block_id = i / DIR_ENTRY_NUM, block_offset = i % DIR_ENTRY_NUM;
        int block_pos;
        if (get_block_pos(inode, block_id, &block_pos)) {
            return -1;
        }
        if (block_pos == -1) {
            continue;
        }
        if (data_read(block_pos, buf)) {
            return -1;
        }
        struct dir_entry* entry = (struct dir_entry*)(buf + block_offset * DIR_ENTRY_SIZE);
        if (entry->inode_pos == 0) {
            continue;
        }
        if (callback(entry, context)) {
            return 0;
        }
    }
    return 0;
}
```

## 附加思考

将 `./Makefile` 中的 `./fuse -s` 中的单线程模式选项 `-s` 去掉，即可开启多线程模式。

### 并发问题

### 互斥访问

### 并发互斥逻辑 `pthread`