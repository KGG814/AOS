/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/* Simple operating system interface */

#ifndef _SOS_H
#define _SOS_H

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <sel4/sel4.h>

//for ttyout stuff 
#define SYSCALL_ENDPOINT_SLOT  (1)

/* System calls for SOS */

/* Endpoint for talking to SOS */
#define SOS_IPC_EP_CAP     (0x1)
#define TIMER_IPC_EP_CAP   (0x2)

/* Limits */
#define MAX_IO_BUF 0x1000
#define N_NAME 32

/* file modes */
#define FM_EXEC  1
#define FM_WRITE 2
#define FM_READ  4
typedef int fmode_t;

/* stat file types */
#define ST_FILE 1   /* plain file */
#define ST_SPECIAL 2    /* special (console) file */
typedef int st_type_t;


typedef struct {
  st_type_t st_type;    /* file type */
  fmode_t   st_fmode;   /* access mode */
  unsigned  st_size;    /* file size in bytes */
  long      st_ctime;   /* file creation time (ms since booting) */
  long      st_atime;   /* file last access (open) time (ms since booting) */
} sos_stat_t;

typedef int pid_t;

typedef struct {
  pid_t     pid;
  unsigned  size;       /* in pages */
  unsigned  stime;      /* start time in msec since booting */
  char      command[N_NAME];    /* Name of exectuable */

} sos_process_t;

/*
 * The message used to hold the syscall number
 */
#define SYSCALL 0

/* A dummy starting syscall */
#define SOS_SYSCALL0 		0
/* A syscall for writing to libserial */
#define SOS_WRITE    		1
/* A syscall for getting the timestamp */
#define TIMESTAMP    		2
/* A syscall for setting the brk for the process */
#define BRK 				3
/* A syscall to sleep the user process for a specified amount of time */
#define USLEEP 				4
/* A syscall to open a file and return a file handle */
#define OPEN 				5
/* A syscall to close a file */
#define CLOSE 				6
/* A syscall to read a specified number of bytes from a file */
#define READ 				7 
/* A syscall to write a specified number of bytes to a file */
#define WRITE   			8
/* GETDIRENT */
#define GETDIRENT           9
/* STAT */
#define STAT 				10
/* I/O system calls */

/* Print to the proper console.  You will need to finish these implementations */
//in sos.c
extern size_t sos_write(const void *data, size_t count);
//in sos.c
extern size_t sos_read(void *data, size_t count);

//in sos_file.c
int sos_sys_open(const char *path, fmode_t mode);
/* Open file and return file descriptor, -1 if unsuccessful
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */

//in sos_file.c
int sos_sys_close(int file);
/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
 */

//in sos_file.c
int sos_sys_read(int file, char *buf, size_t nbyte);
/* Read from an open file, into "buf", max "nbyte" bytes.
 * Returns the number of bytes read.
 * Will block when reading from console and no input is presently
 * available. Returns -1 on error (invalid file).
 */

//in sos_file.c
int sos_sys_write(int file, const char *buf, size_t nbyte);
/* Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */

//in sos_file.c
int sos_getdirent(int pos, char *name, size_t nbyte);
/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */

//in sos_file.c
int sos_stat(const char *path, sos_stat_t *buf);
/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */

//in sos_proc.c
pid_t sos_process_create(const char *path);
/* Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */

//in sos_proc.c
int sos_process_delete(pid_t pid);
/* Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */

//in sos_proc.c
pid_t sos_my_id(void);
/* Returns ID of caller's process. */

//in sos_proc.c
int sos_process_status(sos_process_t *processes, unsigned max);
/* Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */

//in sos_proc.c
pid_t sos_process_wait(pid_t pid);
/* Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */

//in sos.c
int64_t sos_sys_time_stamp(void);
/* Returns time in microseconds since booting.
 */

//in sos.c
void sos_sys_usleep(int msec);
/* Sleeps for the specified number of milliseconds.
 */


/*************************************************************************/
/*                                   */
/* Optional (bonus) system calls                     */
/*                                   */
/*************************************************************************/

int sos_share_vm(void *adr, size_t size, int writable);
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

#endif
