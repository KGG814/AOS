#include "syscalls.h"
/* Open file and return file descriptor, -1 if unsuccessful
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */
int handle_open(void) {

}

/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
 */
int handle_close(void) {

}

/* Read from an open file, into "buf", max "nbyte" bytes.
 * Returns the number of bytes read.
 * Will block when reading from console and no input is presently
 * available. Returns -1 on error (invalid file).
 */
int handle_read(void) {

}


/* Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */
int handle_write(void) {

}


/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
int handle_getdirent(void) {

}


/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
int handle_stat(void) {

}


/* Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
int handle_process_create(void) {

}


/* Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
int handle_process_delete(void) {

}


/* Returns ID of caller's process. */
int handle_my_id(void) {

}


/* Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
int handle_process_status(void) {

}


/* Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
int handle_process_wait(void) {

}


/* Returns time in microseconds since booting.
 */
int handle_time_stamp(void) {

}


/* Sleeps for the specified number of milliseconds.
 */
int handle_usleep(int msec) {

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
int handle_share_vm(void);
