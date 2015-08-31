#include <stdlib.h>
#include <sos.h>
#include "file_table.h"
#include "vfs.h"
#include "pagetable.h"
#include <sel4/types.h>
#include <sys/debug.h>

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
    //open stdin, stdout, stderr
    if (fh_open(as, "console", O_WRONLY) != 0) {
        return -1;
    }
    if (fh_open(as, "console", O_WRONLY) != 1) {
        fd_close(as, 0);
        return -1;
    }
    if (fh_open(as, "console", O_WRONLY) != 2) {
        fd_close(as, 0);
        fd_close(as, 1);
        return -1;
    }
    return 0;
}


int fh_open(addr_space *as, char *path, fmode_t mode) {
    int fd = FT_ERR; //assume we have failed
    /* Turn the user ptr buff into a kernel ptr */
    vnode* vn = vfs_open(path, mode);
    if (vn == NULL) {
        //failed      
        return fd;
    }

    int i = 0;
    while (oft[i] != NULL && i < SOS_MAX_FILES) {
        ++i;
    }

    //no room in oft or fdt
    if (i == SOS_MAX_FILES) {
        vfs_close(vn);
        return FT_ERR_OFT_FULL;
    }

    while (as->file_table[fd] != INVALID_FD && fd < PROCESS_MAX_FILES) {
        ++fd;
    }  
    if (fd == PROCESS_MAX_FILES) {
        vfs_close(vn); 
        return FT_ERR_FDT_FULL;
    }

    file_handle* fh = malloc(sizeof(file_handle));
    if (fh == NULL) {
        vfs_close(vn);
        return FT_ERR;
    }

    oft[i] = fh;
    as->file_table[fd] = i;
    fh->flags = mode;
    fh->offset = 0;
    fh->vn = vn;
    fh->ref_count = 0;
    return fd;
} 

int fd_close(addr_space* as, int file) {
    //assert(0);
    if (file < 0 || file >= PROCESS_MAX_FILES) {
        return -1;
    }
    int oft_index = as->file_table[file];
    if (oft_index == INVALID_FD) {
        return FT_ERR;
    }

    file_handle* handle = oft[oft_index];
    if (handle == NULL) {
        return FT_ERR;
    }

    handle->ref_count--;
    int err = 0;
    if (handle->ref_count == 0) {
        err = vfs_close(handle->vn);
        free(handle);
    }

    as->file_table[file] = INVALID_FD;
    return err;
    /* Generate and send response */
}

