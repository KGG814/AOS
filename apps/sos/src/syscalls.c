#include "syscalls.h"
#include "file_table.h"
#include "pagetable.h"

#include <clock/clock.h>
#include <sos.h>
#include <sos/vmem_layout.h>
#define verbose 5
#include <sys/debug.h>

void wake_process(uint32_t id, void* data) {
    //(void *) data;
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_Send((seL4_CPtr)data, reply);
}

void handle_syscall0(seL4_CPtr reply_cap, addr_space* as) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, 0);
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
    int err = fh_open(as, (char*)k_ptr, mode, reply_cap);
    if (err == FILE_TABLE_CALLBACK) {
        return; //return and let process wait for callback
    } else {
        send_seL4_reply(reply_cap, err);
    }
}

/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
*/
void handle_close(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    int file =  (int) seL4_GetMR(1);
    /* Get the vnode using the process filetable and OFT*/
    send_seL4_reply(reply_cap, fd_close(as, file));
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

    handle->vn->ops->vfs_read(handle->vn, (char*)k_ptr, nbyte, reply_cap, &(handle->offset));
    
    return;
}

/* Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */
void handle_write(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    int file         =  (int)          seL4_GetMR(1);
    char* buf  =  (char*)        seL4_GetMR(2);
    size_t nbyte     =  (size_t)       seL4_GetMR(3);  
    /* Get the vnode using the process filetable and OFT*/
    int oft_index = as->file_table[file];
    file_handle* handle = oft[oft_index];
    /* Check page boundaries and map in pages if necessary */
    user_buffer_check((seL4_Word)buf, nbyte, as);
    /* Turn the user ptr buff into a kernel ptr */
    seL4_Word k_ptr = user_to_kernel_ptr((seL4_Word)buf, as);
    /* Call the write vnode op */
    handle->vn->ops->vfs_write(handle->vn, (char*)k_ptr, nbyte, reply_cap, &(handle->offset));  
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
    int oft_index = 0;
    /* Check page boundaries and map in pages if necessary */
    user_buffer_check((seL4_Word)name, nbyte, as);
    /* Turn the user ptr buff into a kernel ptr */
    seL4_Word k_ptr = user_to_kernel_ptr((seL4_Word)name, as);
    /* Call the getdirent vnode op */
    vfs_getdirent(pos, (char*)k_ptr, nbyte, reply_cap); 
}


/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
void handle_stat(seL4_CPtr reply_cap, addr_space* as) {
    /* Get syscall arguments */
    const char* path =  (char*)        seL4_GetMR(1);
    sos_stat_t* buf  =  (sos_stat_t*)  seL4_GetMR(2);
    /* Check page boundaries and map in pages if necessary */
    user_buffer_check((seL4_Word)path, 256, as);  
    user_buffer_check((seL4_Word)buf, sizeof(sos_stat_t), as);  
    /* Turn the user ptrs path and buf into kernel ptrs*/
    seL4_Word k_ptr1 = user_to_kernel_ptr((seL4_Word)path, as);
    seL4_Word k_ptr2 = user_to_kernel_ptr((seL4_Word)buf, as);
    /* Call the stat vnode op */
    vfs_stat((char*)k_ptr1, (sos_stat_t*)k_ptr2, reply_cap);
    /* Generate and send response */
}

void handle_brk(seL4_CPtr reply_cap, addr_space* as) {
	seL4_Word newbrk = seL4_GetMR(1);
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    uintptr_t ret;
    dprintf(0, "newbrk: %p\n", newbrk);
    if (!newbrk) {
        ret = PROCESS_VMEM_START;
    } else if (newbrk < PROCESS_SCRATCH && newbrk > PROCESS_VMEM_START) {
        ret = newbrk;
        as->brk = newbrk;
        dprintf(0, "as->brk: %p\n", as->brk);
    } else {
        ret = 0;
    }
    seL4_SetMR(0, ret);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}

/* Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
void handle_process_create(void) {
}


/* Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
void handle_process_delete(void) {
}


/* Returns ID of caller's process. */
void handle_my_id(void) {
}


/* Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
void handle_process_status(void) {
}


/* Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
void handle_process_wait(void) {
}


/* Returns time in microseconds since booting.
 */
void handle_time_stamp(seL4_CPtr reply_cap, addr_space* as) {
	timestamp_t timestamp = time_stamp();
	seL4_SetMR(0, (seL4_Word)(UPPER_32(timestamp)));
	seL4_SetMR(1, (seL4_Word)(LOWER_32(timestamp)));
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}


/* Sleeps for the specified number of milliseconds.
 */
void handle_usleep(seL4_CPtr reply_cap, addr_space* as) {
    int usec = seL4_GetMR(1) * 1000;
    register_timer(usec, &wake_process, (void *)reply_cap);
    printf("Timestamp registered\n");
}


void send_seL4_reply(seL4_CPtr reply_cap, int ret) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, ret);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}

/*************************************************************************/
/*                                   */
/* Optional (bonus) system calls                     */
/*                                   */
/*************************************************************************/
/* Make VM region ["adr","adr"+"size") sharable by other processes.
 * If "writable" is non-zero, other processes may have write access to the
 * shared region. Both, "adr" and "size" must be divisible by the page size.
 *
 * In order for a page to be shared, all participating processes must execute
 * the system call specifying an interval including that page.
 * Once a page is shared, a process may write to it if and only if all
 * _other_ processes have set up the page as shared writable.
 *
 * Returns 0 if successful, -1 otherwise (invalid address or size).
 */
int handle_share_vm(void) {
	return 0;
}
