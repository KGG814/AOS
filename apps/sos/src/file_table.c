#include <sos.h>
#include "file_table.h"
#include "vfs.h"
#include "proc.h"
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

int fdt_init(int pid) {
    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        proc_table[pid]->file_table[i] = INVALID_FD;
    }
    proc_table[pid]->n_files_open = 0;

    //open stdin as null
    if (fh_open(pid, "null", O_RDONLY, (seL4_CPtr) 0) != 0) {
        return -1;
    }

    if (fh_open(pid, "console", O_WRONLY, (seL4_CPtr) 0) != 1) {
        fd_close(pid, 0);
        return -1;
    }

    if (fh_open(pid, "console", O_WRONLY, (seL4_CPtr) 0) != 2) {
        fd_close(pid, 0);
        fd_close(pid, 1);
        return -1;
    }
    return 0;
}

int fh_open(int pid, char *path, fmode_t mode, seL4_CPtr reply_cap) {
    
    int err;
    if (proc_table[pid]->n_files_open == PROCESS_MAX_FILES) {
        return -1;
    }

    vnode* vn = vfs_open(path, mode, pid, reply_cap, &err);
    if (vn == NULL || err < 0) {
        return FILE_TABLE_ERR;
    } else if (err == VFS_CALLBACK) {
        // Wait for callback
        return FILE_TABLE_CALLBACK;
    }

    int fd = add_fd(vn, pid);
    if (fd == -1) {
        dprintf(0, "failed to open %s.\n", path);
        // Error, so delete vnode
        vn->ops->vfs_close(vn);
    }

    proc_table[pid]->n_files_open++;

    return fd;
} 

int fd_close(int pid, int file) {
    //assert(0);
    printf("starting close\n");
    if (file < 0 || file >= PROCESS_MAX_FILES) {
        return -1;
    }
    int oft_index = proc_table[pid]->file_table[file];
    if (oft_index == INVALID_FD) {
        return FILE_TABLE_ERR;
    }

    file_handle* handle = oft[oft_index];
    if (handle == NULL) {
        return FILE_TABLE_ERR;
    }

    int err = handle->vn->ops->vfs_close(handle->vn);
    free(handle);

    proc_table[pid]->file_table[file] = INVALID_FD;
    oft[oft_index] = NULL;
    printf("close done\n");
    return err;
}

int add_fd(vnode* vn, int pid) {
    int i = 0;
    while (oft[i] != NULL && i < SOS_MAX_FILES) {
        ++i;
    }

    //no room in oft or fdt
    if (i == SOS_MAX_FILES) {
        return INVALID_FD;
    }
    int fd = 0;
    while (proc_table[pid]->file_table[fd] != INVALID_FD && fd < PROCESS_MAX_FILES) {
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
    proc_table[pid]->file_table[fd] = i;
    fh->offset = 0;
    fh->vn = vn;
    return fd;
}
