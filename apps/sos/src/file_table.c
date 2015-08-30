#include <stdlib.h>
#include <sos.h>
#include "file_table.h"
#include "syscalls.h"
#include "vfs.h"

#define SOS_MAX_FILES 2048

#define INVALID_FD -1

//this is an open file table 
file_handle* oft[SOS_MAX_FILES]; 

int oft_init(void) {
    for (int i = 0; i < SOS_MAX_FILES; i++) {
        oft[i] = NULL;
    }

    return 0;
} 
//taken from cs3231 asst2 solution 
static int attach_console(sos_stat_t *f, fmode_t mode) {
    return 0;
}
int fdt_init(addr_space *as) {
    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        as->file_table[i] = INVALID_FD;
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
    const char *path =  (char*)        seL4_GetMR(1);
    fmode_t mode     =  (fmode_t)      seL4_GetMR(2);
    int fd = -1; //assume we have failed

    vnode* vn = vfs_open(path, mode);
    if (vn == NULL) {
        //failed      
        send_seL4_reply(reply_cap, fd);
        return;
    }

    int i = 0;
    while (oft[i] != NULL && i < SOS_MAX_FILES) {
        ++i;
    }
    while (as->file_table[fd] != INVALID_FD && fd < PROCESS_MAX_FILES) {
        ++fd;
    }

    //no room in oft or fdt
    if (i == SOS_MAX_FILES || fd == PROCESS_MAX_FILES) {
        vfs_close(vn, mode);
        send_seL4_reply(reply_cap, fd);
        return;
    }

    file_handle* fh = malloc(sizeof(file_handle));
    if (fh == NULL) {
        vfs_close(vn, mode);
        send_seL4_reply(reply_cap, fd);
        return;
    }
    
    oft[i] = fh;
    as->file_table[fd] = i;
    fh->flags = mode;
    fh->offset = 0;
    fh->vn = vn;
    fh->ref_count = 0;

    send_seL4_reply(reply_cap, fd);
}

/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
*/
void handle_close(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    int file         =  (int)          seL4_GetMR(1);
    /* Find the vnode and close it */
    (void)file;
    /* 9242_TODO Get the OFT reference from the process filetable */
    /* 9242_TODO Decrement ref count*/
    /* 9242_TODO If 0, call vnode_close */
    /* 9242_TODO If 0, delete and fix up your next free OFT index list however you have implemented it */
    /* 9242_TODO Delete the process filetable reference (if we have refcounts at the process level as well, then check for that too) */
    int returnVal = 0;
    printf("Close not implemented yet\n");
    /* Generate and send response */
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, returnVal);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
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
    /* Find the vnode and do the read vnode call */
    (void)file;
    (void)buf;
    (void)nbyte;
    /* 9242_TODO Get the OFT reference from the process filetable */
    /* 9242_TODO Call the read vnode op */
    int bytes_read = 0;
    printf("Read not implemented yet\n");
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
    /* Find the vnode and do the write vnode call */
    (void)file;
    (void)buf;
    (void)nbyte;
    /* 9242_TODO Get the OFT reference from the process filetable */
    /* 9242_TODO Call the write vnode op */
    int bytes_written = 0;
    printf("Write not implemented yet\n");
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
    /* Find the vnode and do the write getdirent call */
    (void)pos;
    (void)name;
    (void)nbyte;
    /* 9242_TODO Get the OFT reference from the process filetable */
    /* 9242_TODO Call the getdirent vnode op */
    int bytes_returned = 0;
    printf("GetDirent not implemented yet\n");
    /* Generate and send response */
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1); 
    seL4_SetMR(0,  bytes_returned);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}


/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
void handle_stat(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    const char* path =  (char*)        seL4_GetMR(1);
    sos_stat_t* buf  =  (sos_stat_t*)  seL4_GetMR(2);  
    /* Find the vnode and do the write stat call */
    (void)path;
    (void)buf;
    /* 9242_TODO Get the OFT reference from the process filetable */
    /* 9242_TODO Call the stat vnode op */
    int returnVal = 0;
    printf("Stat not implemented yet\n");
    /* Generate and send response */
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1); 
    seL4_SetMR(0,  returnVal);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}
