#ifndef _FILE_TABLE_H_
#define _FILE_TABLE_H_

#include <sos.h>
#include "vfs.h"
#include <sel4/sel4.h>

typedef struct _file_handle {
    int flags; //store flags here
    seL4_Word offset; //offset for reads/writes
    vnode *vn;

    //possibly need a lock for this structure
    uint32_t ref_count;
} file_handle;

typedef struct _fhandle_table {
    int file_descriptor[PROCESS_MAX_FILES]; /* file descriptor table */
} fhandle_table;


int sos_sys_write(int file, const char *buf, size_t nbyte);
//TODO move this into the process struct. for now this is for just the single 
//user process
fhandle_table fdt;

#endif /*_FILE_TABLE_H_*/
