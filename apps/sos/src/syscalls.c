#include "syscalls.h"

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

void handle_brk(seL4_CPtr reply_cap, addr_space* as) {
	seL4_Word newbrk = seL4_GetMR(1);
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    uintptr_t ret;
    dprintf(0, "newbrk: %p\n", newbrk);
    if (!newbrk) {
        ret = PROCESS_VMEM_START;
    } else if (newbrk < PROCESS_SCRATCH && newbrk > PROCESS_VMEM_START) {
        ret = newbrk;
        dprintf(0, "as->brk: %p\n", as->brk);
        as->brk = newbrk;
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
void handle_time_stamp(seL4_CPtr reply_cap) {
	timestamp_t timestamp = time_stamp();
	seL4_SetMR(0, (seL4_Word)(UPPER_32(timestamp)));
	seL4_SetMR(1, (seL4_Word)(LOWER_32(timestamp)));
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}


/* Sleeps for the specified number of milliseconds.
 */
void handle_usleep(seL4_CPtr reply_cap) {
    int usec = seL4_GetMR(1) * 1000;
    register_timer(usec, &wake_process, (void *)reply_cap);
    printf("Timestamp registered\n");
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
