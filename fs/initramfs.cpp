#include "fs/initramfs.h"
#include "fs/vfs.h"
#include "fs/ramfs.h"
#include "syserrno.h"
#include <string.h>
#include <kprint.h>


#define CPIO_NEWC_MAGIC "070701"
#define CPIO_HEADER_SIZE 110
#define CPIO_TRAILER "TRAILER!!!"

#define CPIO_S_IFMT   0170000
#define CPIO_S_IFDIR  0040000
#define CPIO_S_IFREG  0100000
#define CPIO_S_IFLNK  0120000

#define CPIO_OFF_MAGIC     0
#define CPIO_OFF_MODE      14
#define CPIO_OFF_FILESIZE  54
#define CPIO_OFF_NAMESIZE  94
#define CPIO_HEADER_SIZE   110


static inline size_t cpio_align4(size_t n) {
    return (n + 3) & ~(size_t)3;
}
static inline bool safe_add(size_t a, size_t b, size_t* out) {
    *out = a + b;
    return *out >= a;
}

static bool parse_hex(const char* s, size_t len, uint64_t* out) {
    *out = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        uint64_t nibble;
        if (c >= '0' && c <= '9')      nibble = (uint64_t)(c - '0');
        else if (c >= 'A' && c <= 'F') nibble = (uint64_t)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') nibble = (uint64_t)(c - 'a' + 10);
        else return false;
        *out = (*out << 4) | nibble;
    }
    return true;
}

static bool read_hex_field(const uint8_t* buf, size_t off, size_t len, uint64_t* out) {
    return parse_hex((const char*)(buf + off), len, out);
}

static void normalize_path(const char* raw, char* out, size_t max_len) {
    const char* p = raw;
    while (p[0] == '.' && p[1] == '/') p += 2;
    if (p[0] == '/') p++;

    if (p[0] == '.' && p[1] == '\0') {
        out[0] = '\0';
        return;
    }
    
    size_t len = strlen(p);
    if (len >= max_len) len = max_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

static vfs::Dentry* ensure_parent_dir(const char* path) {
    char path_buf[256];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(path_buf)) return nullptr;
    
    memcpy(path_buf, path, len + 1);
    char* last_slash = strrchr(path_buf, '/');

    if (!last_slash) {
        return vfs::path_walk("/"); 
    }
    *last_slash = '\0';
    
    char absolute_path[sizeof(path_buf) + 1];
    absolute_path[0] = '/';
    memcpy(absolute_path + 1, path_buf, len + 1);
    vfs::Dentry* parent = vfs::path_walk(absolute_path);
    if (!IS_ERR(parent)) return parent;
    
    vfs::Dentry* current = vfs::path_walk("/");
    if (!current) return nullptr;
    
    char* token = path_buf;
    char work_buf[256];
    memcpy(work_buf, path_buf, strlen(path_buf) + 1);
    token = work_buf;
    
    while (token && *token) {
        char* next_slash = strchr(token, '/');
        size_t name_len = next_slash ? (size_t)(next_slash - token) : strlen(token);
        
        char name[256];
        if (name_len >= sizeof(name)) { vfs::dput(current); return nullptr; }
        memcpy(name, token, name_len);
        name[name_len] = '\0';
        
        vfs::Dentry* child = vfs::d_lookup(current, name);
        
        if (child) {
            vfs::dget(child);
        } else {
            vfs::Inode* new_ino = ramfs::create_dir(current, name);
            if (!new_ino) {
                kout << "initramfs: failed to create intermediate dir '" << name << "'\n";
                vfs::dput(current);
                return nullptr;
            }
            child = vfs::d_lookup(current, name);
            if (!child) {
                kout << "initramfs: created dir but cannot lookup\n";
                vfs::dput(current);
                return nullptr;
            }
            vfs::dget(child);
        }
        
        vfs::dput(current);
        current = child;
        
        token = next_slash ? (next_slash + 1) : nullptr;
    }
    
    return current;
}

long initramfs_load(const uint8_t* archive, size_t size) {
    if (!archive || size < CPIO_HEADER_SIZE) {
        kout << "initramfs: invalid archive\n";
        return -1;
    }

    size_t offset = 0;
    long count = 0;
    char name_buf[256];

    kout << "initramfs: parsing " << size << " bytes.\n";

    while (offset + CPIO_HEADER_SIZE <= size) {
        const uint8_t* hdr = archive + offset;

        if (memcmp(hdr + CPIO_OFF_MAGIC, CPIO_NEWC_MAGIC, 6) != 0) {
            kout << "initramfs: bad magic at offset " << offset << "\n";
            return -1;
        }

        uint64_t namesize, filesize, mode_raw;
        if (!read_hex_field(hdr, CPIO_OFF_NAMESIZE, 8, &namesize) ||
            !read_hex_field(hdr, CPIO_OFF_FILESIZE, 8, &filesize)) {
            kout << "initramfs: bad hex fields at offset " << offset << "\n";
            return -1;
        }

        size_t name_offset = offset + CPIO_HEADER_SIZE;

        size_t name_end;
        if (!safe_add(offset, CPIO_HEADER_SIZE, &name_offset) ||
            !safe_add(name_offset, namesize, &name_end)) {
            kout << "initramfs: overflow in name calculation\n";
            return -1;
        }

        size_t data_offset_raw;
        if (!safe_add(name_offset, namesize, &data_offset_raw)) { /* ... */ }
        size_t data_offset = cpio_align4(data_offset_raw);
        if (data_offset < data_offset_raw) {
            kout << "initramfs: align4 overflow on data_offset\n";
            return -1;
        }

        size_t data_end;
        if (!safe_add(data_offset, filesize, &data_end)) { /* ... */ }
        size_t next_offset = cpio_align4(data_end);
        if (next_offset < data_end) {
            kout << "initramfs: align4 overflow on next_offset\n";
            return -1;
        }

        if (namesize == 0 || name_end > size || data_end > size || next_offset > size) {
            kout << "initramfs: out of bounds at offset " << offset << "\n";
            return -1;
        }

        kout << "initramfs: [" << count << "] offset=" << offset
            << " name='" << (const char*)(archive + name_offset) << "'"
            << " namesize=" << namesize
            << " filesize=" << filesize
            << " next=" << next_offset << "\n";

        const char* raw_name = (const char*)(archive + name_offset);
        if (raw_name[namesize - 1] != '\0') {
            kout << "initramfs: name not null-terminated\n";
            return -1;
        }

        if (strcmp(raw_name, CPIO_TRAILER) == 0)
            break;

        normalize_path(raw_name, name_buf, sizeof(name_buf));

        if (name_buf[0] == '\0') {
            offset = next_offset;
            continue;
        }

        if (!read_hex_field(hdr, CPIO_OFF_MODE, 8, &mode_raw)) {
            kout << "initramfs: bad mode\n";
            return -1;
        }
        uint32_t type = (uint32_t)mode_raw & CPIO_S_IFMT;

        vfs::Dentry* parent = ensure_parent_dir(name_buf);
        if (!parent) {
            kout << "initramfs: failed to resolve parent for '" << name_buf << "'\n";
            offset = next_offset;
            continue;
        }

        const char* base_name = name_buf;
        const char* slash = strrchr(name_buf, '/');
        if (slash) base_name = slash + 1;

        if (type == CPIO_S_IFDIR) {
            vfs::Inode* ino = ramfs::create_dir(parent, base_name);
            if (ino) count++;
        } 
        else if (type == CPIO_S_IFREG) {
            const uint8_t* data = archive + data_offset;
            vfs::Inode* ino = ramfs::create_file(parent, base_name, data, filesize);
            if (ino) count++;
        } 
        else if (type == CPIO_S_IFLNK) {
            kout << "initramfs: skipping symlink '" << name_buf << "'\n";
        } 
        else {
            kout << "initramfs: skipping unsupported type " << type << " for '" << name_buf << "'\n";
        }

        vfs::dput(parent);

        offset = next_offset;
    }

    kout << "initramfs: successfully loaded " << count << " entries.\n";
    return count;
}