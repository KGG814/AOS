#include <sel4/types.h>
#include "ut_manager/ut.h"
#include <cspace/cspace.h>
#include <mapping.h>
#include <sys/panic.h>
#include <stdlib.h>
#include <sys/debug.h>
#include "pagetable.h"
#include "frametable.h"
#include <sos/vmem_layout.h>


#define PAGEDIR_SIZE   4096
#define PAGE_SIZE   4096
#define BOTTOM(x)  (((x) & 0x3FF000) >> 12)
#define TOP(x)  (((x) & 0xFFC00000) >> 22)
#define PAGE_MASK   0xFFFFF000
#define verbose 5

seL4_Word** page_directory;

int page_init(void) {
    frame_alloc((seL4_Word*)&page_directory);
    //page_directory = (seL4_Word**)malloc(PAGEDIR_SIZE*sizeof(char));
    return 0;
}

int sos_map_page (int ft_index, seL4_Word vaddr, seL4_ARM_PageDirectory pd) {

	seL4_Word dir_index = TOP(vaddr);
	seL4_Word page_index = BOTTOM(vaddr);
	/* Check that the page table exists */
	if (page_directory[dir_index] == NULL) {;
        int index = frame_alloc((seL4_Word*)&page_directory[dir_index]);
        assert(index > FT_OK);
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

    return err;
}

void handle_vm_fault(seL4_Word badge, seL4_ARM_PageDirectory pd) {

    seL4_CPtr reply_cap;
    seL4_Word page_vaddr;
    seL4_Word fault_vaddr = seL4_GetMR(1);
    fault_vaddr &= PAGE_MASK;
    int err = 0;
    dprintf(0, "Handling fault at: 0x%08x\n", fault_vaddr);
    dprintf(0, "Morecore base is currently: 0x%08x\n", 0/*get_morecore_base()*/);
    reply_cap = cspace_save_reply_cap(cur_cspace);
    /* Get the page of the fault address*/
    int ft_index = frame_alloc(&page_vaddr);
    assert(ft_index > FT_OK);
    /* Stack pages*/
    if ((fault_vaddr >= PROCESS_STACK_BOT && fault_vaddr < PROCESS_STACK_TOP)) {
        err = sos_map_page(ft_index, fault_vaddr, pd);
    /* IPC Pages */
    } else if ((fault_vaddr >= PROCESS_IPC_BUFFER && fault_vaddr < PROCESS_IPC_BUFFER_END)) {
        err = sos_map_page(ft_index, fault_vaddr, pd);
    /* VMEM */
    } else if((fault_vaddr >= PROCESS_VMEM_START) && (fault_vaddr < PROCESS_SCRATCH /*get_morecore_base()*/)) {
        err = sos_map_page(ft_index, fault_vaddr, pd);
    /* Scratch */
    } else if((fault_vaddr >= PROCESS_SCRATCH)) {
        err = sos_map_page(ft_index, fault_vaddr, pd);
    
    } else {
      err = 42;
      frame_free(ft_index);
    }
    if (err) {
        dprintf(0, "Something went wrong in handle_vm_fault: %d\n", err);
    }
    

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, 0);
    seL4_Send(reply_cap, reply);
    cspace_free_slot(cur_cspace, reply_cap);
}