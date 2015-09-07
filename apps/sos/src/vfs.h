#ifndef _VFS_H_
#define _VFS_H_ 

//return codes
#define VFS_OK              (0)

//warning codes
#define VFS_CALLBACK        (1)

//error codes
#define VFS_ERR             (-1) //generic error
#define VFS_ERR_NOT_DIR     (-2) //given thing wasn't a directory

//9242_TODO get rid of this to actually use sos.h's conception of permissions
//instead of musl's
#define O_ACCMODE (03)
#define O_RDONLY  (00)
#define O_WRONLY  (01)
#define O_RDWR    (02)

#define CALLBACK (-3)

#include <sos.h>
#include <clock/clock.h>
#include <serial/serial.h>
#include <nfs/nfs.h>
#include "proc.h"
typedef struct _vnode vnode;
typedef struct _vnode_ops vnode_ops;

/* function declarations for functions that aren't fs specific */
vnode*  vfs_open(const char* path
                ,fmode_t mode
                ,addr_space *as
                ,seL4_CPtr reply_cap
                ,int *err);
void     vfs_getdirent(int pos
                     ,char *name
                     ,size_t nbyte
                     ,seL4_CPtr reply_cap
                     );
void    vfs_stat(const char *path
                ,seL4_Word buf
                ,seL4_CPtr reply_cap
                ); 

void vfs_init(void);

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
    void (*vfs_write)(vnode *vn
                     ,const char *buf
                     ,size_t nbyte
                     ,seL4_CPtr reply_cap
                     ,int *offset
                     ,addr_space *as
                     );
    void (*vfs_read)(vnode *vn
                    ,char *buf
                    ,size_t nbyte
                    ,seL4_CPtr reply_cap
                    ,int *offset
                    ,addr_space *as
                    );
    int (*vfs_close)(vnode *vn);
};

#endif /* _VFS_H_ */
