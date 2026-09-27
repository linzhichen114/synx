#pragma once


#define EPERM            1       /* Operation not permitted */
#define ENOENT           2       /* No such file or directory */
#define EIO              5       /* I/O error */
#define ENOMEM           12      /* Out of memory */
#define EACCES           13      /* Permission denied */
#define EEXIST           17      /* File exists */
#define ENOTDIR          20      /* Not a directory */
#define EISDIR           21      /* Is a directory */
#define EINVAL           22      /* Invalid argument */
#define ENOSPC           28      /* No space left on device */
#define ENAMETOOLONG     36      /* File name too long */
#define ENOSYS           38      /* Function not implemented */

#define MAX_ERRNO       4095

#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

template<typename T>
static inline T* ERR_PTR(long error) {
    return reinterpret_cast<T*>(error);
}

static inline long PTR_ERR(const void *ptr) {
    return reinterpret_cast<long>(ptr);
}

static inline bool IS_ERR(const void *ptr) {
    return IS_ERR_VALUE(ptr);
}