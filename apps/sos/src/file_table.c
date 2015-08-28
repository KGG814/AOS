#include <stdlib.h>
#include <sos.h>
#include "file_table.h"
#include "syscalls.h"

#define SOS_MAX_FILES 2048

#define INVALID_FD -1

//this is an open file table 
sos_stat_t *oft[SOS_MAX_FILES]; 

int oft_init(void) {
    for (int i = 0; i < SOS_MAX_FILES; i++) {
        oft[i] = NULL;
    } 
    return 0;
} 
//taken from cs3231 asst2 solution 
static int attach_console(sos_stat_t *f, fmode_t mode) {
    f->st_fmode = mode;
    f->st_type = ST_SPECIAL;
    //TODO set other fields

    return 0;
}
int fdt_init(void) {
    for (int i = 3; i < PROCESS_MAX_FILES; i++) {
        fdt.file_descriptor[i] = INVALID_FD;
    }
    
    
    fdt.file_descriptor[0] = malloc(sizeof(sos_stat_t));
    if (fdt.file_descriptor[0] == NULL) {
    
    }
}

/* Open file and return file descriptor, -1 if unsuccessful
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */
int handle_open(void) {

/*
    if (strncmp("console", path, strlen("console")) == 0) {
        if (mode & FM_READ) {
            if (con_read) {
                //console is already open for reading 
                return -1;
            }     
            files[fd] = malloc(sizeof(sos_stat_t));
            if (files[fd] == NULL) {
                return -1; 
            }
            files[fd]->st_type = ST_SPECIAL;
            files[fd]->st_fmode = mode & 0x7; 
            con_read = 1;
        } 
        return fd;
    } else {
        printf("Error opening file: %s\n", path);
    }
    */
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
