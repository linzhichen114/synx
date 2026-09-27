#include "fs/vfs.h"
#include "mem/slab.h"
#include "kprint.h"
#include "string.h"
#include "syserrno.h"
#include "sysdef.h"


#define ERR_PTR_DENTRY(err) reinterpret_cast<vfs::Dentry*>(static_cast<long>(err))
#define ERR_PTR_FILE(err)   reinterpret_cast<vfs::File*>(static_cast<long>(err))

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
        if (dentry->parent) {
            Dentry** pp = &dentry->parent->child;
            while (*pp) {
                if (*pp == dentry) {
                    *pp = dentry->sibling;
                    break;
                }
                pp = &(*pp)->sibling;
            }
        }
        if (dentry->inode) iput(dentry->inode);
        slab::free(dentry, sizeof(Dentry));
    }
}

__attribute__((noinline))
Dentry* d_alloc(const char* name, Inode* inode, Dentry* parent) {
    Dentry* d = (Dentry*)slab::alloc(sizeof(Dentry));
    
    volatile Dentry* vd = d;
    if (!vd) {
        kout << "vfs: (d_alloc) FATAL - slab::alloc returned NULL for sizeof(Dentry)=" 
             << sizeof(Dentry) << endl;
        return nullptr;
    }
    
    if ((uint64_t)vd < 0xFFFF800000000000ULL) {
        kout << "vfs: (d_alloc) CORRUPTION: invalid pointer " << hex << (void*)vd << dec << endl;
        kernel_panic("vfs: bad pointer from slab");
    }
    
    d = (Dentry*)vd;
    
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

Dentry* d_lookup(Dentry* parent, const char* name) {
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
    if (!path || path[0] != '/') return ERR_PTR_DENTRY(-EINVAL);
    if (!g_root_mount || !g_root_mount->sb || !g_root_mount->sb->root) 
        return ERR_PTR_DENTRY(-ENOENT);

    Dentry* current = g_root_mount->mount_point; 
    if (!current) return ERR_PTR_DENTRY(-ENOENT);
    
    dget(current); 

    const char* ptr = path;
    while (*ptr == '/') ptr++;

    while (*ptr) {
        char component[256];
        size_t len = 0;
        while (ptr[len] && ptr[len] != '/') {
            if (len >= sizeof(component) - 1) {
                dput(current);
                return ERR_PTR_DENTRY(-ENAMETOOLONG);
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
                return ERR_PTR_DENTRY(-ENOTDIR);
            }

            Dentry* next = d_lookup(current, component);
            
            if (!next) {
                if (!current->inode->i_ops || !current->inode->i_ops->lookup) {
                    dput(current);
                    return ERR_PTR_DENTRY(-ENOENT);
                }
                
                Dentry* temp_d = d_alloc(component, nullptr, current);
                if (!temp_d) {
                    dput(current);
                    return ERR_PTR_DENTRY(-ENOMEM);
                }
                
                next = current->inode->i_ops->lookup(current->inode, temp_d, component);
                
                if (!next) {
                    dput(temp_d);
                    dput(current);
                    return ERR_PTR_DENTRY(-ENOENT);
                }
            }

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
        mnt->mount_point = target; 
    }

    mnt->next = g_mount_list;
    g_mount_list = mnt;

    kout << "vfs: Mounted filesystem (magic=" << hex << sb->magic << dec << ") at " << mount_point << endl;
    return VFS_OK;
}
File* open(const char* path, uint32_t flags) {
    if (!path || path[0] != '/') return ERR_PTR_FILE(-EINVAL);
    
    // 1. 分离父目录路径和文件名
    char path_buf[256];
    size_t len = strlen(path);
    if (len >= sizeof(path_buf)) return ERR_PTR_FILE(-ENAMETOOLONG);
    memcpy(path_buf, path, len + 1);
    
    char* last_slash = strrchr(path_buf, '/');
    const char* filename = last_slash ? (last_slash + 1) : path_buf;
    if (last_slash) *last_slash = '\0';
    
    const char* parent_path = (last_slash && path_buf[0]) ? path_buf : "/";
    
    // 2. 获取父目录 dentry (使用 IS_ERR 检查)
    Dentry* parent_dent = path_walk(parent_path);
    if (IS_ERR(parent_dent)) return (File*)parent_dent; // 透传错误码
    
    // 3. 在父目录下查找目标文件
    Dentry* target_dent = d_lookup(parent_dent, filename);
    
    if (!target_dent) {
        // 目标不存在
        if (!(flags & O_CREAT)) {
            dput(parent_dent);
            return ERR_PTR_FILE(-ENOENT); 
        }
        
        // 【O_CREAT 实现】调用父目录 inode 的 create 操作
        if (!parent_dent->inode->i_ops || !parent_dent->inode->i_ops->create) {
            dput(parent_dent);
            return ERR_PTR_FILE(-ENOSYS); 
        }
        
        // 分配一个临时 dentry 传给 create
        Dentry* new_dent = d_alloc(filename, nullptr, parent_dent);
        if (!new_dent) { 
            dput(parent_dent); 
            return ERR_PTR_FILE(-ENOMEM); 
        }
        
        long ret = parent_dent->inode->i_ops->create(
            parent_dent->inode, new_dent, 0644
        );
        
        if (ret < 0) {
            // 创建失败。
            // 注意：不能直接 slab::free(new_dent)！因为 d_alloc 已经把它
            // 链接到了 parent_dent->child 链表中。必须调用 dput 让它
            // 安全地从链表中摘除并释放。
            dput(new_dent);
            dput(parent_dent);
            return ERR_PTR_FILE(ret); // 透传文件系统返回的具体错误码
        }
        
        // 成功，new_dent->inode 应该已经被文件系统层设置好了
        target_dent = new_dent;
    } else {
        // 目标已存在
        if (flags & O_EXCL) {
            dput(parent_dent);
            return ERR_PTR_FILE(-EEXIST); 
        }
        // 如果已存在且不需要 O_EXCL，我们需要增加它的引用计数
        dget(target_dent);
    }
    
    dput(parent_dent);
    
    // 4. 构建 File 对象
    File* f = (File*)slab::alloc(sizeof(File));
    if (!f) { 
        dput(target_dent); 
        return ERR_PTR_FILE(-ENOMEM); 
    }
    memset(f, 0, sizeof(File));
    
    f->dentry = target_dent; // File 持有 dentry 引用
    f->offset = 0;
    f->flags = flags;
    f->f_ops = target_dent->inode->f_ops;
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