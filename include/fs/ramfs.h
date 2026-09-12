#pragma once
#include "fs/vfs.h"

namespace ramfs {

struct InodeData {
    uint8_t* buffer;
    size_t   capacity;
};

struct SimpleDirent {
    uint32_t d_ino;
    char d_name[256];
};

vfs::SuperBlock* init();
vfs::Inode* create_file(vfs::Dentry* parent_dir, const char* name, const void* data, size_t size);
vfs::Inode* create_dir(vfs::Dentry* parent_dir, const char* name);

} // namespace ramfs