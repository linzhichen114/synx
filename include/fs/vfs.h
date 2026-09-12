#pragma once
#include <stdint.h>
#include <stddef.h>

namespace vfs {

constexpr long VFS_OK          = 0;
constexpr long VFS_ERR_NOENT   = -2;   // No such file or directory
constexpr long VFS_ERR_IO      = -5;   // I/O error
constexpr long VFS_ERR_NOMEM   = -12;  // Out of memory
constexpr long VFS_ERR_BUSY    = -16;  // Device or resource busy
constexpr long VFS_ERR_NOTDIR  = -20;  // Not a directory
constexpr long VFS_ERR_ISDIR   = -21;  // Is a directory
constexpr long VFS_ERR_INVAL   = -22;  // Invalid argument

constexpr uint32_t O_RDONLY = 0x00;
constexpr uint32_t O_WRONLY = 0x01;
constexpr uint32_t O_RDWR   = 0x02;
constexpr uint32_t O_CREAT  = 0x40;
constexpr uint32_t O_TRUNC  = 0x200;
constexpr uint32_t O_APPEND = 0x400;
constexpr uint32_t O_DIRECTORY = 0x10000;

enum class FileType : uint16_t {
    Regular   = 0100000, // S_IFREG
    Directory = 0040000, // S_IFDIR
    CharDev   = 0020000, // S_IFCHR
    BlockDev  = 0060000, // S_IFBLK
};

struct Inode;
struct Dentry;
struct File;
struct SuperBlock;
struct Mount;

struct InodeOps {
    Dentry* (*lookup)(Inode* dir, Dentry* dentry, const char* name);
    long (*create)(Inode* dir, Dentry* dentry, uint16_t mode);
};

struct FileOps {
    long (*read)(File* file, void* buf, size_t size, uint64_t offset);
    long (*write)(File* file, const void* buf, size_t size, uint64_t offset);
    long (*readdir)(File* file, void* dirent, size_t count);
};

struct SuperOps {
    void (*put_super)(SuperBlock* sb);
    void (*destroy_inode)(Inode* inode);
};

struct Inode {
    uint32_t    ino;
    FileType    type;
    uint64_t    size;
    volatile int32_t ref_count;
    
    SuperBlock* sb;
    InodeOps*   i_ops;
    FileOps*    f_ops;
    
    void*       private_data;
};

struct Dentry {
    char        name[256];
    Inode*      inode;
    Dentry*     parent;
    Dentry*     child;
    Dentry*     sibling;
    
    volatile int32_t ref_count;
};

struct File {
    Dentry*     dentry;
    uint64_t    offset; 
    uint32_t    flags;
    FileOps*    f_ops;
    volatile int32_t ref_count;
};

struct SuperBlock {
    uint32_t    magic;
    uint32_t    block_size;
    Inode*      root;
    SuperOps*   s_ops;
    void*       fs_info;
};

struct Mount {
    SuperBlock* sb;
    Dentry*     mount_point;
    Mount*      next;
};

Dentry* d_alloc(const char* name, Inode* inode, Dentry* parent);
long mount(SuperBlock* sb, const char* mount_point);
Dentry* path_walk(const char* path);
File* open(const char* path, uint32_t flags);
long read(File* file, void* buf, size_t size);
long write(File* file, const void* buf, size_t size);
void close(File* file);
void iget(Inode* inode);
void iput(Inode* inode);
void dget(Dentry* dentry);
void dput(Dentry* dentry);

} // namespace vfs