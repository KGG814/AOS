#ifndef _VFS_H_
#define _VFS_H_ 

#define VOP(vn, op, args...) vn->ops->op(vn, ##args)

#define vfs_write(vn, args...) VOP(vn, _vfs_write, ##args)
#define vfs_read(vn, args...) VOP(vn, _vfs_read, ##args)
#define vfs_close(vn, args...) VOP(vn, _vfs_close, ##args)

//return codes
#define VFS_OK              (0)

//warning codes
#define VFS_CALLBACK        (1)

//error codes
#define VFS_ERR             (-1) //generic error
#define VFS_ERR_NOT_DIR     (-2) //given thing wasn't a directory

#define O_ACCMODE (03)
#define O_RDONLY  (00)
#define O_WRONLY  (01)
#define O_RDWR    (02)

#define CALLBACK (-3)

#include <sos/sos.h>
#include <clock/clock.h>
#include <nfs/nfs.h>
#include "proc.h"

typedef struct _vnode vnode;
typedef struct _vnode_ops vnode_ops;

typedef struct _vfs_stat_args {
    seL4_CPtr reply_cap;
    seL4_Word buf;
    seL4_Word kpath;
    int pid;
} vfs_stat_args;

typedef struct _getdirent_args {
    seL4_CPtr reply_cap;
    seL4_Word to_get;
    seL4_Word entries_received;
    seL4_Word buf;
    int pid;
    size_t nbyte;
} getdirent_args;

//store vnodes as a linked list for now
struct _vnode {
    fmode_t         fmode;   /* access mode */
    unsigned        size;    /* file size in bytes */
    timeval_t     ctime;   /* file creation time (ms since booting) */
    timeval_t     atime;   /* file last access (open) time (ms since booting) */

    //long            mtime;   /* file last modify time */
    void*           fs_data;
  
    struct _vnode   *next;

    //inspired by OS/161
    const vnode_ops *ops;

    //empty array for filename
    char name[];
};

struct _vnode_ops {
    /* function pointers for fs specific functions */
    void (*_vfs_write)(vnode *vn
                     ,const char *buf
                     ,size_t nbyte
                     ,seL4_CPtr reply_cap
                     ,int *offset
                     ,int pid
                     );
    void (*_vfs_read)(vnode *vn
                    ,char *buf
                    ,size_t nbyte
                    ,seL4_CPtr reply_cap
                    ,int *offset
                    ,int pid
                    );
    int (*_vfs_close)(vnode *vn);
};

void vfs_init(void);
void vnode_insert(vnode *vn);

//removes a vnode from the list
int vnode_remove(vnode *vn);

/* function declarations for functions that aren't fs specific */
vnode*  vfs_open(const char* path
                ,fmode_t mode
                ,int pid
                ,seL4_CPtr reply_cap
                ,int *err);
void     vfs_getdirent(int pos
                     ,char *name
                     ,size_t nbyte
                     ,seL4_CPtr reply_cap
                     ,int pid
                     );
void    vfs_stat(const char *path
                ,seL4_Word buf
                ,seL4_CPtr reply_cap
                ,int pid
                ); 

void vfs_stat_wrapper (int pid, seL4_CPtr reply_cap, void* args, int err);

#endif /* _VFS_H_ */
