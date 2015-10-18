#include "syscalls.h"
#include "file_table.h"
#include "pagetable.h"
#include "devices.h"
#include <string.h>
#include <clock/clock.h>
#include <sos/sos.h>
#include <sos/vmem_layout.h>

#include "proc.h"

#define verbose 5
#include <sys/debug.h>
#include "debug.h"

extern int console_status;
//callback for waking sleeping processes
static void wake_process(uint32_t id, void* data); 

void handle_syscall0(seL4_CPtr reply_cap, int pid) {
    send_seL4_reply(reply_cap, pid, 0);
}

void handle_sos_write(seL4_CPtr reply_cap, int pid) {
    /*
    char data[sizeof(seL4_Word)*seL4_MsgMaxLength];
    // Go through each message and transfer the word
    seL4_Word* currentWord = (seL4_Word*)data;
    for (int i = 1; i <= num_args; i++) {
        *currentWord = seL4_GetMR(i);
        currentWord++;
    }
    serial_send(serial_handler, data, num_args*sizeof(seL4_Word));
    */

    //this is now deprecated
    send_seL4_reply(reply_cap, pid, -1);
}

/* Open file and return file descriptor, -1 if unsuccessful
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.res
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */
void handle_open(seL4_CPtr reply_cap, int pid) {
    if (SOS_DEBUG) printf("handle_open\n");
    /* Get syscall arguments */
    char *path =  (char*)        seL4_GetMR(1);
    fmode_t mode     =  (fmode_t)      seL4_GetMR(2);
 
    if (check_region((seL4_Word)path, (seL4_Word)MAXNAMLEN)) {
        send_seL4_reply(reply_cap, pid, EFAULT);
        return;
    } 

    if (path == NULL) {
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    char *kpath = malloc(MAXNAMLEN + 1);
    if (kpath == NULL) {
        send_seL4_reply(reply_cap, pid, -1); 
        return;
    }
    memset(kpath, 0, MAXNAMLEN + 1);
    file_open_args *fo_args = malloc(sizeof(file_open_args));
    if (fo_args == NULL) {
        free(kpath);
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    fo_args->path = kpath;
    fo_args->mode = mode;

    copy_in_args *args = malloc(sizeof(copy_in_args));
    if (args == NULL) {
        free(kpath);
        free(fo_args);
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    
    args->count = 0;
    args->nbyte = MAXNAMLEN;
    args->usr_ptr = (seL4_Word) path;
    args->k_ptr = (seL4_Word) kpath;
    args->cb = fh_open_wrapper;
    args->cb_args = fo_args;
    copy_in(pid, reply_cap, args, 0);
}

/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
*/
void handle_close(seL4_CPtr reply_cap, int pid) {
    /* Get syscall arguments */
    int file =  (int) seL4_GetMR(1);

    if (file < 0 || file >= PROCESS_MAX_FILES) {
        send_seL4_reply(reply_cap, pid, -1);
    }

    /* Get the vnode using the process filetable and OFT*/
    send_seL4_reply(reply_cap, pid, fd_close(pid, file));
}


/* Read from an open file, into "buf", max "nbyte" bytes.
 * Returns the number of bytes read.
 * Will block when reading from console and no input is presently
 * available. Returns -1 on error (invalid file).
 */
void handle_read(seL4_CPtr reply_cap, int pid) {
    /* Get syscall arguments */
    int file         =  (int)          seL4_GetMR(1);
    char* buf        =  (char*)        seL4_GetMR(2);
    size_t nbyte     =  (size_t)       seL4_GetMR(3);  
    if (SOS_DEBUG) printf("handle read\n");
    //check filehandle is actually in range
    if (file < 0 || file >= PROCESS_MAX_FILES) {
        send_seL4_reply(reply_cap, pid, -1);
        return;
    } 
    if (buf == NULL) {
        send_seL4_reply(reply_cap, pid, 0);
        return;
    }
    /* Get the vnode using the process filetable and OFT*/
    int oft_index = proc_table[pid]->file_table[file];
    file_handle* handle = oft[oft_index];
    /* Check page boundaries and map in pages if necessary */
    /* Turn the user ptr buff into a kernel ptr */
    /* Call the read vnode op */

    vfs_read(handle->vn, buf, nbyte, reply_cap, &(handle->offset), pid);
    if (SOS_DEBUG) printf("handle read finished\n");
    return;
}

/* Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */
void handle_write(seL4_CPtr reply_cap, int pid) {
    /* Get syscall arguments */
    int file         =  (int)          seL4_GetMR(1);
    char* buf        =  (char*)        seL4_GetMR(2);
    size_t nbyte     =  (size_t)       seL4_GetMR(3);  
    if (SOS_DEBUG) printf("handle write\n");
    //printf("Write syscall handler %d, %p, %d\n", file, buf, nbyte);
    //check filehandle is actually in range
    if (file < 0 || file >= PROCESS_MAX_FILES) {
        if (SOS_DEBUG) printf("out of range fd: %d\n", file);
        send_seL4_reply(reply_cap, pid, -1);
        return;
    } 

    if (buf == NULL) {
        send_seL4_reply(reply_cap, pid, 0);
        return;
    }

    /* Get the vnode using the process filetable and OFT*/
    int oft_index = proc_table[pid]->file_table[file];
    file_handle* handle = oft[oft_index];

    /* Check page boundaries and map in pages if necessary */;
    /* Call the write vnode op */
    vfs_write(handle->vn, buf, nbyte, reply_cap, &(handle->offset), pid);
    if (SOS_DEBUG) printf("handle write finished\n");  
}


/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
void handle_getdirent(seL4_CPtr reply_cap, int pid) {
    /* Get syscall arguments */
    int pos          =  (int)          seL4_GetMR(1);
    char* name       =  (char*)        seL4_GetMR(2);
    size_t nbyte     =  (size_t)       seL4_GetMR(3);
    if (check_region((seL4_Word)name, (seL4_Word)nbyte)) {
        send_seL4_reply(reply_cap, pid, EFAULT);
        return;
    } 
    /* Call the getdirent vnode op */
    vfs_getdirent(pos, name, nbyte, reply_cap, pid); 
}


/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
void handle_stat(seL4_CPtr reply_cap, int pid) {
    if (SOS_DEBUG) printf("handle stat\n");
    /* Get syscall arguments */
    seL4_Word   path =                 seL4_GetMR(1);
    sos_stat_t* buf  =  (sos_stat_t*)  seL4_GetMR(2);
    if (check_region((seL4_Word)path, (seL4_Word)MAXNAMLEN)) {
        send_seL4_reply(reply_cap, pid, EFAULT);
        return;
    } 

    if ((void *)path == NULL) {
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    char *kpath = malloc(sizeof(char) * (MAXNAMLEN + 1));
    if (kpath == NULL) {
        send_seL4_reply(reply_cap, pid, -1);
    }
    kpath[MAXNAMLEN] = '\0';
    vfs_stat_args *stat_args = malloc(sizeof(vfs_stat_args));
    if (stat_args == NULL) {
        free(kpath);
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    stat_args->buf = (seL4_Word) buf;
    stat_args->kpath = (seL4_Word) kpath;

    copy_in_args *args = malloc(sizeof(copy_in_args));
    if (args == NULL) {
        free(stat_args);
        free(kpath);
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    args->count = 0;
    args->nbyte = MAXNAMLEN;
    args->usr_ptr = (seL4_Word) path;
    args->k_ptr = (seL4_Word) kpath;
    args->cb = vfs_stat_wrapper;
    args->cb_args = stat_args;
    printf("Copy args %p stat args %p\n", args, stat_args);
    copy_in(pid, reply_cap, args, 0);
    if (SOS_DEBUG) printf("handle stat finished\n");
    /* Call stat */
    //vfs_stat((char*) kpath, (seL4_Word) buf, reply_cap, pid);
}

void handle_brk(seL4_CPtr reply_cap, int pid) {
	seL4_Word newbrk = seL4_GetMR(1);
    uintptr_t ret;
    if (SOS_DEBUG) printf("newbrk: %p\n", (void *)newbrk);
    if (!newbrk) {
        ret = PROCESS_VMEM_START;
    } else if (newbrk < PROCESS_SCRATCH && newbrk > PROCESS_VMEM_START) {
        ret = newbrk;
        proc_table[pid]->brk = newbrk;
        if (SOS_DEBUG) printf("proc_table[pid]->brk: %p\n", (void *)proc_table[pid]->brk);
    } else {
        ret = 0;
    }
    send_seL4_reply(reply_cap, pid, ret);
}

/* Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
void handle_process_create(seL4_CPtr reply_cap, int pid) {
    seL4_Word user_path = (seL4_Word) seL4_GetMR(1);

    //9242_TODO change this to a copy in
    seL4_Word kernel_path = user_to_kernel_ptr(user_path, pid);

    printf("Starting process %s\n", (char *) kernel_path);
    start_process_args *process_args = malloc(sizeof(start_process_args));
    process_args->app_name = (char *)kernel_path;
    process_args->fault_ep = _sos_ipc_ep_cap;
    process_args->priority = TTY_PRIORITY;
    printf("Setting callback %p\n", handle_process_create_cb);
    process_args->cb = handle_process_create_cb;
    process_args->cb_args = NULL;
    process_args->parent_pid = pid;
    start_process(pid, reply_cap, process_args);
}

/* Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
void handle_process_delete(seL4_CPtr reply_cap, int pid) {
    // 9242_TODO If current process has parent, reply on wait cap if they are the process being waited for
    int to_delete = (int) seL4_GetMR(1);
    proc_table[pid]->wait_cap = reply_cap;
    kill_process(pid, to_delete, reply_cap); 
}

/* Returns ID of caller's process. */
void handle_my_id(seL4_CPtr reply_cap, int pid) {
    addr_space *as = proc_table[pid];
    child_proc *cp = as->children;
    int i = 0;
    while (cp != NULL) {
        printf("Child %i: %d\n", i, cp->pid);
        cp = cp->next;
        i++;
    }
    send_seL4_reply(reply_cap, pid, pid);
}


/* Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
void handle_process_status(seL4_CPtr reply_cap, int pid) {
    sos_process_t* processes = (sos_process_t *) seL4_GetMR(1);
    unsigned max_processes   = (unsigned)        seL4_GetMR(2);
    if (check_region((seL4_Word) processes
                    ,(seL4_Word) (max_processes * sizeof(sos_process_t)))) 
    {
        send_seL4_reply(reply_cap, pid, 0);
        return;
    } 
    process_status(reply_cap, pid, processes, max_processes);
}

/* Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
void handle_process_wait(seL4_CPtr reply_cap, int pid) {
    // 9242_TODO add pid -1 case, with global list
    if (pid == -1) {
        add_to_wait_list(pid);
    }
    proc_table[pid]->wait_cap = reply_cap;
    
    /*if (proc_table[pid]->reader_status == CURR_READ) {
        proc_table[pid]->reader_status = CHILD_READ;
        console_status = CONSOLE_READ_CLOSE;
    }*/
}


/* Returns time in microseconds since booting.
 */
//does not block
void handle_time_stamp(seL4_CPtr reply_cap, int pid) {
    proc_table[pid]->status &= ~PROC_BLOCKED;
	timestamp_t timestamp = time_stamp();
	seL4_SetMR(0, (seL4_Word)(UPPER_32(timestamp)));
	seL4_SetMR(1, (seL4_Word)(LOWER_32(timestamp)));
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}


/* Sleeps for the specified number of milliseconds.
 */
//this blocks 
void handle_usleep(seL4_CPtr reply_cap, int pid) {
    int usec = seL4_GetMR(1) * 1000;
    int ret = register_timer(usec, &wake_process, (void *)pid);
    proc_table[pid]->wait_cap = reply_cap;
    if (ret == 0) { //handle error
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    if (SOS_DEBUG) printf("sleep timer registered\n");
}

//timer callback for usleep
static void wake_process(uint32_t id, void* data) {

    int pid = (int) data;
    printf("pid %d woke\n", pid);
    seL4_CPtr reply_cap = proc_table[pid]->wait_cap;
    send_seL4_reply(reply_cap, pid, 0);
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
