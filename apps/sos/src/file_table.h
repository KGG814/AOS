#ifndef _FILE_TABLE_H_
#define _FILE_TABLE_H_


#include <sos.h>
#include "vfs.h"
#include "proc.h"

#define SOS_MAX_FILES 2048

#define INVALID_FD  (-1)

#define FILE_TABLE_ERR          (-1)
//#define FT_ERR_OFT_FULL (-2)
//#define FT_ERR_FDT_FULL (-3)
#define FILE_TABLE_CALLBACK     (-2)   

typedef struct _file_handle file_handle;
file_handle* oft[SOS_MAX_FILES]; 

struct _file_handle {
    int offset; //offset for reads/writes
    vnode *vn;
};

int fdt_init(int pid);
int oft_init(void);

int fh_open(int pid, char *path, fmode_t mode, seL4_CPtr reply_cap);
void fh_open_wrapper (int pid, seL4_CPtr reply_cap, void* args);
int fd_close(int pid, int file);
int add_fd(vnode* vn, int pid);

#endif /*_FILE_TABLE_H_*/
