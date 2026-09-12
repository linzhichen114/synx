#include "fs/vfs.h"
#include "mem/slab.h"
#include "kprint.h"
#include "string.h"
#include "sysdef.h"

namespace vfs {

static Mount* g_root_mount = nullptr; // 根挂载点
static Mount* g_mount_list = nullptr; // 全局挂载链表头

void iget(Inode* inode) {
    if (inode) atomic_inc(&inode->ref_count);
}

void iput(Inode* inode) {
    if (!inode) return;
    if (atomic_dec_and_test(&inode->ref_count)) {
        inode->sb->s_ops->destroy_inode(inode);
        slab::free(inode, sizeof(Inode));
    }
}

void dget(Dentry* dentry) {
    if (dentry) atomic_inc(&dentry->ref_count);
}

void dput(Dentry* dentry) {
    if (!dentry) return;
    if (atomic_dec_and_test(&dentry->ref_count)) {
        // 递归释放父节点？不，通常由具体 FS 或 dcache shrinker 处理
        // 这里为了简单，直接释放当前 dentry 内存
        slab::free(dentry, sizeof(Dentry));
    }
}

Dentry* d_alloc(const char* name, Inode* inode, Dentry* parent) {
    Dentry* d = (Dentry*)slab::alloc(sizeof(Dentry));
    if (!d) return nullptr;
    
    memset(d, 0, sizeof(Dentry));
    size_t len = strlen(name);
    if (len >= sizeof(d->name)) len = sizeof(d->name) - 1;
    memcpy(d->name, name, len);
    d->name[len] = '\0';
    
    d->inode = inode;
    d->parent = parent;
    d->ref_count = 1;

    if (parent) {
        d->sibling = parent->child;
        parent->child = d;
    }
    
    return d;
}

static Dentry* d_lookup(Dentry* parent, const char* name) {
    Dentry* child = parent->child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->sibling;
    }
    return nullptr;
}

Dentry* path_walk(const char* path) {
    if (!path || path[0] != '/') return nullptr;
    if (!g_root_mount || !g_root_mount->sb || !g_root_mount->sb->root) return nullptr;

    // 从根挂载点的根 dentry 开始
    // 注意：我们需要根 inode 对应的根 dentry。
    // 在真实内核中，sb->s_root 指向根 dentry。这里我们假设 root inode 的 private_data 存了根 dentry，
    // 或者我们在 mount 时创建了一个虚拟的根 dentry。
    // 为了严谨，我们在 mount_fs 中创建一个 root_dentry 存在 sb 中。这里先简化：
    
    Dentry* current = g_root_mount->mount_point; 
    if (!current) return nullptr;
    
    dget(current); // 持有当前节点

    const char* ptr = path;
    while (*ptr == '/') ptr++;

    while (*ptr) {
        char component[256];
        size_t len = 0;
        while (ptr[len] && ptr[len] != '/') {
            if (len >= sizeof(component) - 1) {
                dput(current);
                return nullptr;
            }
            component[len] = ptr[len];
            len++;
        }
        component[len] = '\0';

        if (strcmp(component, ".") == 0) {
            // 保持 current 不变
        } else if (strcmp(component, "..") == 0) {
            if (current->parent) {
                Dentry* old = current;
                current = current->parent;
                dget(current);
                dput(old);
            }
        } else {
            if (current->inode->type != FileType::Directory) {
                dput(current);
                return nullptr;
            }

            Dentry* next = d_lookup(current, component);
            
            if (!next) {
                if (!current->inode->i_ops || !current->inode->i_ops->lookup) {
                    dput(current);
                    return nullptr;
                }
                
                Dentry* temp_d = d_alloc(component, nullptr, current);
                next = current->inode->i_ops->lookup(current->inode, temp_d, component);
                
                if (!next) {
                    slab::free(temp_d, sizeof(Dentry));
                    dput(current);
                    return nullptr;
                }
            }

            // 跨挂载点处理 (Mount Point Crossing)
            // 如果找到的 dentry 是一个挂载点，我们需要跳转到新文件系统的根
            // (这里简化处理，暂不实现复杂的 mount tree 遍历)

            dget(next);
            dput(current);
            current = next;
        }

        ptr += len;
        while (*ptr == '/') ptr++;
    }

    return current;
}

long mount(SuperBlock* sb, const char* mount_point) {
    if (!sb || !sb->root) return VFS_ERR_INVAL;

    Mount* mnt = (Mount*)slab::alloc(sizeof(Mount));
    if (!mnt) return VFS_ERR_NOMEM;
    memset(mnt, 0, sizeof(Mount));
    mnt->sb = sb;

    Dentry* root_d = d_alloc("/", sb->root, nullptr);
    mnt->mount_point = root_d;

    if (!g_root_mount) {
        g_root_mount = mnt;
    } else {
        Dentry* target = path_walk(mount_point);
        if (!target) {
            slab::free(mnt, sizeof(Mount));
            return VFS_ERR_NOENT;
        }
        // 简化处理：直接将 target 作为挂载点
        // 真实内核中需要处理覆盖、busy 检查等
        mnt->mount_point = target; 
    }

    mnt->next = g_mount_list;
    g_mount_list = mnt;

    kout << "vfs: Mounted filesystem (magic=" << hex << sb->magic << dec << ") at " << mount_point << endl;
    return VFS_OK;
}

File* open(const char* path, uint32_t flags) {
    Dentry* dentry = path_walk(path);
    if (!dentry) {
        if (flags & O_CREAT) {
            // TODO: 实现 O_CREAT 逻辑 (需要调用父目录 inode->i_ops->create)
            return nullptr; 
        }
        return nullptr;
    }

    if ((flags & O_DIRECTORY) && dentry->inode->type != FileType::Directory) {
        dput(dentry);
        return nullptr;
    }

    File* f = (File*)slab::alloc(sizeof(File));
    if (!f) {
        dput(dentry);
        return nullptr;
    }
    memset(f, 0, sizeof(File));
    
    f->dentry = dentry;
    f->offset = 0;
    f->flags = flags;
    f->f_ops = dentry->inode->f_ops;
    f->ref_count = 1;

    return f;
}

long read(File* file, void* buf, size_t size) {
    if (!file || !buf) return VFS_ERR_INVAL;
    if ((file->flags & O_WRONLY)) return VFS_ERR_IO;
    if (!file->f_ops || !file->f_ops->read) return VFS_ERR_IO;

    long ret = file->f_ops->read(file, buf, size, file->offset);
    if (ret > 0)
        __atomic_fetch_add(&file->offset, ret, __ATOMIC_SEQ_CST);

    return ret;
}

long write(File* file, const void* buf, size_t size) {
    if (!file || !buf) return VFS_ERR_INVAL;
    if (file->flags == O_RDONLY) return VFS_ERR_IO;
    if (!file->f_ops || !file->f_ops->write) return VFS_ERR_IO;

    uint64_t off = file->offset;
    if (file->flags & O_APPEND) {
        off = file->dentry->inode->size;
    }

    long ret = file->f_ops->write(file, buf, size, off);
    if (ret > 0) {
        __atomic_fetch_add(&file->offset, ret, __ATOMIC_SEQ_CST);
        if (off + ret > file->dentry->inode->size) {
            file->dentry->inode->size = off + ret;
        }
    }
    return ret;
}

void close(File* file) {
    if (!file) return;
    if (atomic_dec_and_test(&file->ref_count)) {
        dput(file->dentry); // 释放对 dentry 的引用
        slab::free(file, sizeof(File));
    }
}

} // namespace vfs