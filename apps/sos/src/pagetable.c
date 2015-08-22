#include <sel4/types.h>
#include "ut_manager/ut.h"
#include <cspace/cspace.h>
#include <mapping.h>
#include <sys/panic.h>
#include <stdlib.h>
#include <sys/debug.h>
#include "pagetable.h"
#include "frametable.h"

seL4_Word** page_directory = (seL4_Word**)0x30000000;
seL4_Word*  page_tables    = (seL4_Word*) 0x30004000;
seL4_CPtr*  cap_directory  = (seL4_CPtr*) 0x40000000;
seL4_CPtr   page_directory_cap;
seL4_CPtr   cap_directory_cap;

#define PAGEDIR_SIZE   4096
#define PAGE_SIZE   4096
#define BOTTOM(x)  ((x) & 0x3FF)
#define TOP(x)  (((x) & 0xFFC00) >> 10)
#define verbose 5

int page_init(void) {
    frame_alloc((seL4_Word*)&page_directory);
    //page_directory = (seL4_Word**)malloc(PAGEDIR_SIZE*sizeof(char));
    return 0;
}

seL4_Word sos_map_page (int ft_index, seL4_Word vaddr, seL4_ARM_PageDirectory pd) {
	seL4_Word dir_index = TOP(vaddr);
	seL4_Word page_index = BOTTOM(vaddr);

    // THIS
	/* Check that the page table exists */
	if (page_directory[dir_index] == NULL) {
        frame_alloc((seL4_Word*)&page_directory[dir_index]);
        //page_directory[dir_index] = (seL4_Word*)malloc(PAGEDIR_SIZE*sizeof(char));
	}
	/* Map into the sos page table 
       ft_index is the lower 20 bits */
	page_directory[dir_index][page_index] = ft_index;
    /* Map into the given process page directory */
    seL4_CPtr frame_cap = cspace_mint_cap(cur_cspace,
                                  cur_cspace,
                                  frametable[ft_index].frame_cap,
                                  seL4_AllRights, 
                                  seL4_CapData_Badge_new(0));


    int err = map_page(frame_cap, pd, vaddr, 
                seL4_AllRights, seL4_ARM_Default_VMAttributes);

    return 0;
}

void handle_vm_fault(seL4_Word badge, seL4_ARM_PageDirectory pd) {
    seL4_CPtr reply_cap;
    seL4_Word page_vaddr;
    seL4_Word fault_vaddr = seL4_GetMR(1);

    reply_cap = cspace_save_reply_cap(cur_cspace);
    
    //dprintf(0, "Handling vm fault at:  0x%08x\n", fault_vaddr);
    /* TODO do this only if it is in the right region */
    /* Alloc and map it in to process page table if it is in stack or heap memory region*/
    int ft_index = frame_alloc(&page_vaddr);
    if (ft_index < FT_OK) {
        /* No memory left */

    } else if ((fault_vaddr > PROCESS_STACK_BOT && fault_vaddr < PROCESS_STACK_TOP) ||
               (fault_vaddr > PROCESS_IPC_BUFFER && fault_vaddr < PROCESS_IPC_BUFFER_END) ||
               (fault_vaddr > PROCESS_SCRATCH) ||
               (fault_vaddr > PROCESS_VMEM_START)) {

    }
    sos_map_page(ft_index, fault_vaddr, pd);

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, 0);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}