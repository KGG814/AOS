#include <sos.h>
#include "file_table.h"
#include "vfs.h"
#include "pagetable.h"
#include <sel4/types.h>
#include <sys/debug.h>
#include <stdlib.h>
#include "syscalls.h"

#define verbose 5

//this is an open file table 

int oft_init(void) {
    for (int i = 0; i < SOS_MAX_FILES; i++) {
        oft[i] = NULL;
    }

    return 0;
} 

int fdt_init(addr_space *as) {
    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        as->file_table[i] = INVALID_FD;
    }

    //open stdin as null
    if (fh_open(as, "null", O_RDONLY, (seL4_CPtr) 0) != 0) {
        return -1;
    }

    if (fh_open(as, "console", O_WRONLY, (seL4_CPtr) 0) != 1) {
        fd_close(as, 0);
        return -1;
    }

    if (fh_open(as, "console", O_WRONLY, (seL4_CPtr) 0) != 2) {
        fd_close(as, 0);
        fd_close(as, 1);
        return -1;
    }
    return 0;
}

int fh_open(addr_space *as, char *path, fmode_t mode, seL4_CPtr reply_cap) {
    
    int err;
    vnode* vn = vfs_open(path, mode, as, reply_cap, &err);
    if (vn == NULL || err < 0) {
        return FILE_TABLE_ERR;
    } else if (err == VFS_CALLBACK) {
        // Wait for callback
        return FILE_TABLE_CALLBACK;
    }

    int fd = add_fd(vn, as);
    if (fd == -1) {
        dprintf(0, "failed to open %s.\n", path);
        // Error, so delete vnode
        vn->ops->vfs_close(vn);
    }

    return fd;
} 

int fd_close(addr_space* as, int file) {
    //assert(0);
    if (file < 0 || file >= PROCESS_MAX_FILES) {
        return -1;
    }
    int oft_index = as->file_table[file];
    if (oft_index == INVALID_FD) {
        return FILE_TABLE_ERR;
    }

    file_handle* handle = oft[oft_index];
    if (handle == NULL) {
        return FILE_TABLE_ERR;
    }

    int err = handle->vn->ops->vfs_close(handle->vn);
    free(handle);

    as->file_table[file] = INVALID_FD;
    oft[oft_index] = NULL;
    return err;
    /* Generate and send response */
}

int add_fd(vnode* vn, addr_space* as) {
    int i = 0;
    while (oft[i] != NULL && i < SOS_MAX_FILES) {
        ++i;
    }

    //no room in oft or fdt
    if (i == SOS_MAX_FILES) {
        return INVALID_FD;
    }
    int fd = 0;
    while (as->file_table[fd] != INVALID_FD && fd < PROCESS_MAX_FILES) {
        ++fd;
    }  
    if (fd == PROCESS_MAX_FILES) {
        return INVALID_FD;
    }
    
    file_handle* fh = malloc(sizeof(file_handle));
    if (fh == NULL) {
        return INVALID_FD;
    }
    oft[i] = fh;
    as->file_table[fd] = i;
    fh->offset = 0;
    fh->vn = vn;
    return fd;
}
