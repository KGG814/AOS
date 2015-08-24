#ifndef _MY_DICKKKKK_
#define _MY_DICKKKKK_

#include <cspace/cspace.h>

#define CAP_TABLE_PAGES 4

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
} addr_space; 



#endif /* _MY_DICKKKKK_ */