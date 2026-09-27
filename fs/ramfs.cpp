#include "fs/ramfs.h"
#include "mem/slab.h"
#include <string.h>
#include "kprint.h"

namespace ramfs {

static uint32_t g_next_ino = 1;
static vfs::SuperBlock* g_sb = nullptr;

static vfs::Dentry* ramfs_lookup(vfs::Inode* dir, vfs::Dentry* dentry, const char* name);
static long ramfs_read(vfs::File* file, void* buf, size_t size, uint64_t offset);
static long ramfs_readdir(vfs::File* file, void* dirent, size_t count);

static vfs::InodeOps g_ramfs_inode_ops = {
    .lookup = ramfs_lookup,
    .create = nullptr // 暂时不支持运行时动态创建文件
};

static vfs::FileOps g_ramfs_file_ops = {
    .read = ramfs_read,
    .write = nullptr, // 暂时只读
    .readdir = ramfs_readdir
};


static vfs::Inode* alloc_inode(vfs::FileType type) {
    vfs::Inode* inode = (vfs::Inode*)slab::alloc(sizeof(vfs::Inode));
    if (!inode) return nullptr;
    
    memset(inode, 0, sizeof(vfs::Inode));
    inode->ino = g_next_ino++;
    inode->type = type;
    inode->size = 0;
    inode->ref_count = 1;
    inode->sb = g_sb;
    inode->i_ops = &g_ramfs_inode_ops;
    inode->f_ops = &g_ramfs_file_ops;

    InodeData* priv = (InodeData*)slab::alloc(sizeof(InodeData));
    if (!priv) {
        slab::free(inode, sizeof(vfs::Inode));
        return nullptr;
    }
    memset(priv, 0, sizeof(InodeData));
    inode->private_data = priv;
    
    return inode;
}

static vfs::Dentry* ramfs_lookup(vfs::Inode* dir, vfs::Dentry* dentry, const char* name) {
    if (dir->type != vfs::FileType::Directory) return nullptr;

    vfs::Dentry* parent_dentry = dentry->parent;
    if (!parent_dentry) return nullptr;
    
    vfs::Dentry* child = parent_dentry->child;
    while (child) {
        if (child != dentry && strcmp(child->name, name) == 0) {
            vfs::iget(child->inode);
            return child; 
        }
        child = child->sibling;
    }
    
    return nullptr;
}

static long ramfs_read(vfs::File* file, void* buf, size_t size, uint64_t offset) {
    vfs::Inode* inode = file->dentry->inode;
    if (inode->type != vfs::FileType::Regular) return vfs::VFS_ERR_INVAL;
    
    InodeData* priv = (InodeData*)inode->private_data;
    if (!priv || !priv->buffer) return 0; // empty file
    
    if (offset >= inode->size) return 0; // EOF
    
    if (offset + size > inode->size)
        size = inode->size - offset;
    
    memcpy(buf, priv->buffer + offset, size);
    return (long)size;
}

static long ramfs_readdir(vfs::File* file, void* buf, size_t count) {
    vfs::Inode* inode = file->dentry->inode;
    if (inode->type != vfs::FileType::Directory) return vfs::VFS_ERR_NOTDIR;
    
    vfs::Dentry* parent_dentry = file->dentry;
    vfs::Dentry* child = parent_dentry->child;

    size_t index = 0;
    size_t written = 0;
    SimpleDirent* out = (SimpleDirent*)buf;
    
    while (child && written < count) {
        if (index >= file->offset) {
            out[written].d_ino = child->inode->ino;
            strncpy(out[written].d_name, child->name, sizeof(out[written].d_name) - 1);
            out[written].d_name[sizeof(out[written].d_name) - 1] = '\0';
            written++;
        }
        child = child->sibling;
        index++;
    }

    file->offset = index; 
    return (long)written;
}

vfs::SuperBlock* init() {
    g_sb = (vfs::SuperBlock*)slab::alloc(sizeof(vfs::SuperBlock));
    if (!g_sb) return nullptr;
    
    memset(g_sb, 0, sizeof(vfs::SuperBlock));
    g_sb->magic = 0x52414D46; // "RAMF"
    g_sb->block_size = 4096;

    vfs::Inode* root_inode = alloc_inode(vfs::FileType::Directory);
    if (!root_inode) {
        slab::free(g_sb, sizeof(vfs::SuperBlock));
        return nullptr;
    }
    
    g_sb->root = root_inode;
    
    kout << "ramfs: Initialized, Root inode=" << root_inode->ino << endl;
    return g_sb;
}

vfs::Inode* create_file(vfs::Dentry* parent_dir, const char* name, const void* data, size_t size) {
    if (!parent_dir || !parent_dir->inode || parent_dir->inode->type != vfs::FileType::Directory) {
        kout << "ramfs::create_file: invalid parent_dir" << endl;
        return nullptr;
    }
    
    vfs::Inode* inode = alloc_inode(vfs::FileType::Regular);
    if (!inode) return nullptr;
    
    inode->size = size;

    InodeData* priv = (InodeData*)inode->private_data;
    if (size > 0) {
        priv->buffer = (uint8_t*)slab::alloc(size);
        if (!priv->buffer) {
            slab::free(priv, sizeof(InodeData));
            slab::free(inode, sizeof(vfs::Inode));
            return nullptr;
        }
        priv->capacity = size;
        memcpy(priv->buffer, data, size);
    }
    
    vfs::Dentry* d = vfs::d_alloc(name, inode, parent_dir);
    
    volatile vfs::Dentry* vd = d;
    if (!vd) {
        kout << "ramfs::create_file: d_alloc returned nullptr!" << endl;
        if (priv->buffer) slab::free(priv->buffer, priv->capacity);
        slab::free(priv, sizeof(InodeData));
        slab::free(inode, sizeof(vfs::Inode));
        return nullptr;
    }
    
    return inode;
}

vfs::Inode* create_dir(vfs::Dentry* parent_dir, const char* name) {
    if (!parent_dir || parent_dir->inode->type != vfs::FileType::Directory) return nullptr;
    
    vfs::Inode* inode = alloc_inode(vfs::FileType::Directory);
    if (!inode) return nullptr;

    vfs::Dentry* d = vfs::d_alloc(name, inode, parent_dir);
    if (!d) {
        InodeData* priv = (InodeData*)inode->private_data;
        slab::free(priv, sizeof(InodeData));
        slab::free(inode, sizeof(vfs::Inode));
        return nullptr;
    }
    
    return inode;
}

} // namespace ramfs