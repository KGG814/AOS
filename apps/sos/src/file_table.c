#include <sos/sos.h>
#include "file_table.h"
#include "vfs.h"
#include "proc.h"
#include "pagetable.h"
#include <sel4/types.h>
#include <sys/debug.h>
#include <stdlib.h>
#include "syscalls.h"
#include "debug.h"

#define verbose 5

//this is an open file table 

int oft_init(void) {
    for (int i = 0; i < SOS_MAX_FILES; i++) {
        oft[i] = NULL;
    }

    return 0;
} 

int fdt_init(int pid) {
    if (proc_table[pid] == NULL) {
        return -1;
    }
    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        proc_table[pid]->file_table[i] = INVALID_FD;
    }
    proc_table[pid]->n_files_open = 0;

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

void fdt_cleanup(int pid) {
    if (proc_table[pid] == NULL) {
        return;
    }

    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        fd_close(pid, i);
    }
}

void fh_open_wrapper (int pid, seL4_CPtr reply_cap, void* _args, int err) {
    file_open_args * args = (file_open_args *) _args;
    char* path = args->path;
    fmode_t mode = args->mode;
    free(args);
    
    if (err) {
        send_seL4_reply(reply_cap, pid, FILE_TABLE_ERR);
        free(path);
        return;
    }

    int fd = fh_open(pid, path, mode, reply_cap);
    free(path);
    
    if (fd >= FILE_TABLE_ERR) {
        send_seL4_reply(reply_cap, pid, fd);
    }
}

int fh_open(int pid, char *path, fmode_t mode, seL4_CPtr reply_cap) {
    if (SOS_DEBUG) printf("fh_open\n");
    int err;
    if (proc_table[pid]->n_files_open == PROCESS_MAX_FILES) {
        return -1;
    }

    vnode* vn = vfs_open(path, mode, pid, reply_cap, &err);
    if (vn == NULL || err < 0) {
        assert(RTN_ON_FAIL);
        printf("couldn't open a vn. dumping valid filehandles: \n");
        for (int i = 0; i < PROCESS_MAX_FILES; i++) {
            if (proc_table[pid]->file_table[i] != INVALID_FD) {
                vnode *vn = oft[proc_table[pid]->file_table[i]]->vn;
                printf("file %d: %s\n", i, vn->name); 
            }
        }
        return FILE_TABLE_ERR;
    } else if (err == VFS_CALLBACK) {
        // Wait for callback
        if (SOS_DEBUG) printf("waiting for callback in fh_open\n");
        return FILE_TABLE_CALLBACK;
    }

    int fd = add_fd(vn, pid);
    if (fd == -1) {
        dprintf(0, "failed to open %s.\n", path);
        // Error, so delete vnode
        vfs_close(vn);
    }

    proc_table[pid]->n_files_open++;
    if (SOS_DEBUG) printf("fh_open end\n");
    return fd;
} 

int fd_close(int pid, int file) {
    //assert(0);
    if (SOS_DEBUG) printf("starting fd_close\n");
    if (file < 0 || file >= PROCESS_MAX_FILES) {
        return -1;
    }
    int oft_index = proc_table[pid]->file_table[file];
    if (oft_index == INVALID_FD) {
        return FILE_TABLE_ERR;
    }

    printf("doing close on pid %d, fd %d\n", pid, file);

    file_handle* handle = oft[oft_index];
    if (handle == NULL) {
        return FILE_TABLE_ERR;
    }

    int err = vfs_close(handle->vn);
    free(handle);

    proc_table[pid]->file_table[file] = INVALID_FD;
    oft[oft_index] = NULL;
    proc_table[pid]->n_files_open--;
    if (SOS_DEBUG) printf("close done\n");
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
        if (SOS_DEBUG) printf("Couldn't get a valid fd\n");
        return INVALID_FD;
    }
    
    file_handle* fh = malloc(sizeof(file_handle));
    if (fh == NULL) {
        if (SOS_DEBUG) printf("Ran out of memory for fh\n");
        return INVALID_FD;
    }
    oft[i] = fh;
    proc_table[pid]->file_table[fd] = i;
    fh->offset = 0;
    fh->vn = vn;
    return fd;
}
