#ifndef _VFS_H_
#define _VFS_H_ 

//error codes
#define VFS_ERR             (-1) //generic error
#define VFS_ERR_NOT_DIR     (-2) //given thing wasn't a directory

#include <sos.h>
#include <clock/clock.h>
#include <serial/serial.h>

typedef struct _vnode vnode;
typedef struct _vnode_ops vnode_ops;

/* function declarations for functions that aren't fs specific */
vnode*  vfs_open(const char* path, fmode_t mode);
int     vfs_close(vnode *vn);
int     vfs_getdirent(int pos, const char *name, size_t nbyte);
int     vfs_stat(const char *path, sos_stat_t *buf); 

void vfs_init(struct serial *s);

//store vnodes as a linked list for now
struct _vnode {
    fmode_t         fmode;   /* access mode */
    unsigned        size;    /* file size in bytes */
    timestamp_t     ctime;   /* file creation time (ms since booting) */
    timestamp_t     atime;   /* file last access (open) time (ms since booting) */

    //long            mtime;   /* file last modify time */
    void*           fs_data;
  
    struct _vnode   *next;

    //inspired by OS/161
    const vnode_ops *ops;

    //empty array for filename
    char name[];
};

/* 9242_TODO Change these so they take a vnode instead of a file desc */
struct _vnode_ops {
    /* function pointers for fs specific functions */
    int (*vfs_write)(vnode *vn, const char *buf, size_t nbyte);
    int (*vfs_read)(vnode *vn, const char *buf, size_t nbyte);

};

#endif /* _VFS_H_ */
