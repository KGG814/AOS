#ifndef _VFS_H_
#define _VFS_H_ 

#include <sos.h>

typedef struct _vnode vnode;

/* function declarations for functions that aren't fs specific */
vnode*  vfs_open(const char* path, fmode_t mode);
int     vfs_close(vnode *vn);
int     vfs_getdirent(int pos, const char *name, size_t nbyte);
int     vfs_stat(const char *path, sos_stat_t *buf); 

//store vnodes as a linked list for now
struct _vnode {
    fmode_t         fmode;   /* access mode */
    unsigned        size;    /* file size in bytes */
    long            ctime;   /* file creation time (ms since booting) */
    long            atime;   /* file last access (open) time (ms since booting) */
    //long            mtime;   /* file last modify time */
    void*           fs_data;
    
    struct _vnode   *next;

    //inspired by OS/161
    const struct vnode_ops* ops;

    //empty array for filename
    char name[];
};

struct vnode_ops {
    /* function pointers for fs specific functions */
    int (*vfs_write)(int file, const char *buf, size_t nbyte);
    int (*vfs_read)(int file, const char *buf, size_t nbyte);
};

#endif /* _VFS_H_ */