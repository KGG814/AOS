#include <stdlib.h>
#include <sos.h>
#include "file_table.h"
#include "syscalls.h"
#include "vfs.h"
#include "pagetable.h"
#include <sel4/types.h>

#define SOS_MAX_FILES 2048


//this is an open file table 
file_handle* oft[SOS_MAX_FILES]; 

int fh_open(addr_space *as, char *path, fmode_t mode); 
int fd_close(addr_space* as, int file);

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
    if (fh_open(as, "console", FM_READ) != 0) {
        return -1;
    }
    if (fh_open(as, "console", FM_WRITE) != 1) {
        fd_close(as, 0);
        return -1;
    }
    if (fh_open(as, "console", FM_WRITE) != 2) {
        fd_close(as, 0);
        fd_close(as, 1);
        return -1;
    }

    return 0;
}

void send_seL4_reply(seL4_CPtr reply_cap, int ret) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, ret);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}
/* Open file and return file descriptor, -1 if unsuccessful
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.res
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */
void handle_open(seL4_CPtr reply_cap, addr_space* as) {

    /* Get syscall arguments */
    char *path =  (char*)        seL4_GetMR(1);
    fmode_t mode     =  (fmode_t)      seL4_GetMR(2);
    
    seL4_Word k_ptr = user_to_kernel_ptr((seL4_Word)path, as);
    int fd = fh_open(as, k_ptr, mode);
    send_seL4_reply(reply_cap, fd);
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

/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
*/
void handle_close(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    int file =  (int) seL4_GetMR(1);
    /* Get the vnode using the process filetable and OFT*/
    send_seL4_reply(reply_cap, fd_close(file, as));
}

/* Read from an open file, into "buf", max "nbyte" bytes.
 * Returns the number of bytes read.
 * Will block when reading from console and no input is presently
 * available. Returns -1 on error (invalid file).
 */
void handle_read(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    int file         =  (int)          seL4_GetMR(1);
    char* buf        =  (char*)        seL4_GetMR(2);
    size_t nbyte     =  (size_t)       seL4_GetMR(3);  
    /* Get the vnode using the process filetable and OFT*/
    int oft_index = as->file_table[file];
    file_handle* handle = oft[oft_index];
    /* Check page boundaries and map in pages if necessary */
    user_buffer_check((seL4_Word)buf, nbyte, as);
    /* Turn the user ptr buff into a kernel ptr */
    seL4_Word k_ptr = user_to_kernel_ptr((seL4_Word)buf, as);
    /* Call the read vnode op */
    int bytes_read = handle->vn->ops->vfs_read(handle->vn, (char*)k_ptr, nbyte);
    /* Generate and send response */
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1); 
    seL4_SetMR(0, bytes_read);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}


/* Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */
void handle_write(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    int file         =  (int)          seL4_GetMR(1);
    const char* buf  =  (char*)        seL4_GetMR(2);
    size_t nbyte     =  (size_t)       seL4_GetMR(3);  
    /* Get the vnode using the process filetable and OFT*/
    int oft_index = as->file_table[file];
    file_handle* handle = oft[oft_index];
    /* Check page boundaries and map in pages if necessary */
    user_buffer_check((seL4_Word)buf, nbyte, as);
    /* Turn the user ptr buff into a kernel ptr */
    seL4_Word k_ptr = user_to_kernel_ptr((seL4_Word)buf, as);
    /* Call the write vnode op */
    int bytes_written = handle->vn->ops->vfs_write(handle->vn, (char*)k_ptr, nbyte);  
    /* Generate and send response */
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1); 
    seL4_SetMR(0, bytes_written);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}


/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
void handle_getdirent(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    int pos          =  (int)          seL4_GetMR(1);
    char* name       =  (char*)        seL4_GetMR(2);
    size_t nbyte     =  (size_t)       seL4_GetMR(3);  

    /* Get the vnode using the process filetable and OFT*/
    int oft_index = as->file_table[pos];
    if (oft_index == INVALID_FD) {
        send_seL4_reply(reply_cap, oft_index);
        return;
    }
    file_handle* handle = oft[oft_index];
    if (handle == NULL) {
        send_seL4_reply(reply_cap, FT_ERR);
        return;
    }
    /* Check page boundaries and map in pages if necessary */
    user_buffer_check((seL4_Word)name, nbyte, as);
    /* Turn the user ptr buff into a kernel ptr */
    seL4_Word k_ptr = user_to_kernel_ptr((seL4_Word)name, as);
    /* Call the getdirent vnode op */
    int err = handle->vn->ops->vfs_getdirent(oft_index, (char*)k_ptr, nbyte); 

    /* Generate and send response */
    send_seL4_reply(reply_cap, err);
}


/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
void handle_stat(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    const char* path =  (char*)        seL4_GetMR(1);
    sos_stat_t* buf  =  (sos_stat_t*)  seL4_GetMR(2);
    /* Check page boundaries and map in pages if necessary */
    //user_buffer_check((seL4_Word)path, nbyte, as);  
    //user_buffer_check((seL4_Word)buf, nbyte, as);  
    /* Turn the user ptrs path and buf into kernel ptrs*/
    seL4_Word k_ptr1 = user_to_kernel_ptr((seL4_Word)path, as);
    seL4_Word k_ptr2 = user_to_kernel_ptr((seL4_Word)buf, as);
    /* 9242_TODO Find the file in the file table */
    /* Call the stat vnode op */
    //int return_val = handle->vn->ops->vfs_getdirent(oft_index, buf, nbyte);
    int return_val = 0;

    /* Generate and send response */
    send_seL4_reply(reply_cap, return_val);
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
