#ifndef _FILE_TABLE_H_
#define _FILE_TABLE_H_

#include <sel4/sel4.h>
#include <sos.h>
#include "vfs.h"
#include "proc.h"

typedef struct _file_handle {
    int flags; //store flags here
    seL4_Word offset; //offset for reads/writes
    vnode *vn;

    //possibly need a lock for this structure
    uint32_t ref_count;
} file_handle;



int sos_sys_write(int file, const char *buf, size_t nbyte);

int fdt_init(addr_space *as);
int oft_init(void);
#endif /*_FILE_TABLE_H_*/
