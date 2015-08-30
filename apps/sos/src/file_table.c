#include <stdlib.h>
#include <sos.h>
#include "file_table.h"
#include "syscalls.h"
#include "vfs.h"

#define SOS_MAX_FILES 2048

#define INVALID_FD -1

//this is an open file table 
file_handle* oft[SOS_MAX_FILES]; 
int console = INVALID_FD; 
int console_read = 0;

int oft_init(void) {
    /*for (int i = 0; i < SOS_MAX_FILES; i++) {
        oft[i] = NULL;
    } */

    return 0;
} 
//taken from cs3231 asst2 solution 
static int attach_console(sos_stat_t *f, fmode_t mode) {
    return 0;
}
int fdt_init(void) {
    /*for (int i = 3; i < PROCESS_MAX_FILES; i++) {
        fdt.file_descriptor[i] = INVALID_FD;
    }*/
    
    return 0;
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
    vnode* new_vnode = vfs_open(path, mode);
    /* Return a file descriptor to the user for them to use to do operation on the file */
    (void)path;
    (void)mode;
    /* 9242_TODO Search for file in open file table */
    /* 9242_TODO If OFT ref not found: Get the next free index for the open file table*/
    /* 9242_TODO If OFT ref not found:  Make new file handle*/
    /* 9242_TODO Get the next free file index for the process file table*/
    /* 9242_TODO Refer to the newly created handle in the open filetable from the process filetable somehow i.e. pointer or index*/
    /* Notes
     * For the process file table, if we use indices, then we will have space left over in the int in the upper bits to store a linked list for the next free index
     * If you want to do pointers you'll need to figure out some other method since the pointer will take the full 32 bits.
     * 
     * For the OFT, we will also need a list, maybe a next free index within the handle?
     * Also consider what we do if the file is already opened in the process (do we allow multiple handles to the same file?)
    */
    int file_desc = 0;
    printf("Open not implemented yet\n");
    /* Generate and send response */
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, file_desc);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
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
