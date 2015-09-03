#ifndef _FILE_TABLE_H_
#define _FILE_TABLE_H_


#include <sos.h>
#include "vfs.h"
#include "proc.h"

#define SOS_MAX_FILES 2048

#define INVALID_FD  (-1)

#define FT_ERR             (-1)
#define FT_ERR_OFT_FULL    (-2)
#define FT_ERR_FDT_FULL    (-3)

typedef struct _file_handle file_handle;
file_handle* oft[SOS_MAX_FILES]; 

struct _file_handle {
    int offset; //offset for reads/writes
    vnode *vn;

    //possibly need a lock for this structure
    uint32_t ref_count;
};

int fdt_init(addr_space *as);
int oft_init(void);

void fh_open(addr_space *as, char *path, fmode_t mode, seL4_CPtr reply_cap);
int fd_close(addr_space* as, int file);
int add_fd(vnode* vn, addr_space* as);

#endif /*_FILE_TABLE_H_*/
