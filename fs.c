/*
Filesystem Lab disigned and implemented by Liang Junkai,RUC
*/

#include "disk.h"
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DIRMODE S_IFDIR | 0755
#define REGMODE S_IFREG | 0644

// Format the virtual block device: basic filesystem structure, root directory, etc.
// Return 0 if the operation is successful, not 0 otherwise
int mkfs()
{
    return 0;
}

// Query the attributes of a regular file or directory
// Return -ENOENT if the file or directory does not exist
int fs_getattr(const char* path, struct stat* attr)
{
    printf("Getattr is called:%s\n", path);

    *attr = (struct stat) {
        .st_mode = DIRMODE, // Or REGMODE
        .st_nlink = 1,
        .st_uid = getuid(),
        .st_gid = getgid(),
        .st_size = 0,
        .st_atime = time(NULL),
        .st_mtime = time(NULL),
        .st_ctime = time(NULL),
    };
    return 0;
}

// Read all entries in a directory
// Update the `atime` of the directory
// `ls` command can trigger this function
int fs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, [[maybe_unused]] off_t offset, struct fuse_file_info* fi)
{
    printf("Readdir is called:%s\n", path);
    return 0;
}

// Read the contents of a regular file
// Update the `atime` of the file
// `cat` command can trigger this function
// Return the number of bytes read
int fs_read(const char* path, char* buffer, size_t size, off_t offset, struct fuse_file_info* fi)
{
    printf("Read is called:%s\n", path);
    return 0;
}

// Create a regular file
// `touch` and `echo x > file` commands can trigger this function
// Update the `ctime` and `mtime` of the parent directory
// Return -ENOSPC if no enough space or file nodes
int fs_mknod(const char* path, [[maybe_unused]] mode_t mode, [[maybe_unused]] dev_t dev)
{
    printf("Mknod is called:%s\n", path);

    // truncate the file to 0 size

    // utime the file
    return 0;
}

// Create a directory
// `mkdir` command can trigger this function
// Update the `ctime` and `mtime` of the parent directory
// Return -ENOSPC if no enough space or file nodes
int fs_mkdir(const char* path, [[maybe_unused]] mode_t mode)
{
    printf("Mkdir is called:%s\n", path);
    return 0;
}

// Remove a directory
// `rmdir -r` command can trigger this function
// Update the `mtime` and `ctime` of the parent directory
int fs_rmdir(const char* path)
{
    printf("Rmdir is called:%s\n", path);

    // readdir returns empty, and there is no need for recursion
    return 0;
}

// Remove a regular file
// `rm` command can trigger this function
// Update the `mtime` and `ctime` of the parent directory
int fs_unlink(const char* path)
{
    printf("Unlink is callded:%s\n", path);

    // Release the reserved data blocks
    return 0;
}

// Change the name or location of a file or directory
// `mv` command can trigger this function
int fs_rename(const char* oldpath, const char* newpath)
{
    printf("Rename is called:%s\n", newpath);
    return 0;
}

// Write data to a regular file
// Update the `mtime` and `ctime` of the file
// Return the number of bytes written which should be equal to `size`, or 0 on error
int fs_write(const char* path, const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi)
{
    printf("Write is called:%s\n", path);

    // Adjust the size of the file first

    // Write the data to the file
    // `O_APPEND` requires special handling
    if (fi->flags & O_CREAT) {
        fs_mknod(path, 0, 0);
    }

    return 0;
}

// Change the size of a regular file
// `truncate` command can trigger this function
// Update the `ctime` of the file
// Return -ENOSPC if no enough space
int fs_truncate(const char* path, off_t size)
{
    printf("Truncate is called:%s\n", path);
    return 0;
}

// Change the access and modification times of a regular file or directory
// Update the `ctime` of the file
int fs_utime(const char* path, struct utimbuf* buffer)
{
    printf("Utime is called:%s\n", path);
    buffer->actime = time(NULL); // `atime`
    buffer->modtime = time(NULL); // `mtime`

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