#ifndef _PROC_H_
#define _PROC_H_

#include <cspace/cspace.h>
//#include <sos.h>

#define CAP_TABLE_PAGES 4
#define PROCESS_MAX_FILES 16

typedef struct _addr_space {
	seL4_Word vroot_addr;
	seL4_ARM_PageDirectory vroot;

    seL4_Word** page_directory;
    seL4_ARM_PageTable* cap_table[CAP_TABLE_PAGES];

    seL4_Word ipc_buffer_addr;
    seL4_CPtr ipc_buffer_cap;

    seL4_Word tcb_addr;
    seL4_TCB tcb_cap;

    cspace_t *croot;
    seL4_Word brk;
    int file_table[PROCESS_MAX_FILES];
} addr_space; 



#endif /* _PROC_H_ */
