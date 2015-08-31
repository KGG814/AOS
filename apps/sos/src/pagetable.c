#include <sel4/types.h>
#include "ut_manager/ut.h"
#include <cspace/cspace.h>
#include <mapping.h>
#include <sys/panic.h>
#include <stdlib.h>
#include <sys/debug.h>
#include "pagetable.h"
#include "frametable.h"
#include "proc.h"
#include <sos/vmem_layout.h>
#include <assert.h>

#define PAGEDIR_SIZE   4096
#define PAGE_SIZE   4096
#define PT_BOTTOM(x)  (((x) & 0x3FF000) >> 12)
#define PT_TOP(x)  (((x) & 0xFFC00000) >> 22)
#define PAGE_MASK   0xFFFFF000
#define FT_INDEX_MASK 0x000FFFFF
#define verbose 5


int page_init(addr_space* as) {
    seL4_Word vaddr;
    frame_alloc(&vaddr, KMAP);
    as->page_directory = (seL4_Word**) vaddr;
    for (int i = 0; i < CAP_TABLE_PAGES; i++) {
        frame_alloc(&vaddr,KMAP);
        as->cap_table[i] = (seL4_ARM_PageTable*)vaddr;
    }
    return 0;
}

seL4_CPtr sos_map_page (int ft_index, seL4_Word vaddr, seL4_ARM_PageDirectory pd, addr_space* as) {
	seL4_Word dir_index = PT_TOP(vaddr);
	seL4_Word page_index = PT_BOTTOM(vaddr);
	/* Check that the page table exists */
    assert(as->page_directory != NULL);
    int index = 0;
    seL4_Word temp;
	if (as->page_directory[dir_index] == NULL) {
        index = frame_alloc(&temp, KMAP);
        as->page_directory[dir_index] = (seL4_Word*)temp;
        assert(index > FT_OK);
	}
	/* Map into the sos page table 
       ft_index is the lower 20 bits */
    assert(as->page_directory[dir_index] != NULL);
	as->page_directory[dir_index][page_index] = ft_index;

    /* Map into the given process page directory */

    seL4_CPtr frame_cap = cspace_copy_cap(cur_cspace, cur_cspace, frametable[ft_index].frame_cap, seL4_AllRights);
    map_page_user(frame_cap, pd, vaddr, 
                seL4_AllRights, seL4_ARM_Default_VMAttributes, as);
    return frame_cap;
}

void handle_vm_fault(seL4_Word badge, seL4_ARM_PageDirectory pd, addr_space* as) {
    /* 9242_TODO Kill process if invalid memory */
    /* 9242_TODO Instruction faults? */
    seL4_CPtr reply_cap;
    seL4_Word page_vaddr;
    seL4_Word fault_vaddr = seL4_GetMR(1);
    fault_vaddr &= PAGE_MASK;
    int err = 0;
    dprintf(0, "Handling fault at: 0x%08x\n", fault_vaddr);
    reply_cap = cspace_save_reply_cap(cur_cspace);
    /* Get the page of the fault address*/
    int ft_index = frame_alloc(&page_vaddr, KMAP);
    assert(ft_index > FT_OK);
    /* Stack pages*/
    if ((fault_vaddr >= PROCESS_STACK_BOT && fault_vaddr < PROCESS_STACK_TOP)) {
        sos_map_page(ft_index, fault_vaddr, pd, as);
    /* IPC Pages */
    } else if ((fault_vaddr >= PROCESS_IPC_BUFFER && fault_vaddr < PROCESS_IPC_BUFFER_END)) {
        sos_map_page(ft_index, fault_vaddr, pd, as);
    /* VMEM */
    } else if((fault_vaddr >= PROCESS_VMEM_START) && (fault_vaddr < as->brk)) {
        sos_map_page(ft_index, fault_vaddr, pd, as);
    /* Scratch */
    } else if((fault_vaddr >= PROCESS_SCRATCH)) {
        sos_map_page(ft_index, fault_vaddr, pd, as);   
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

seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, addr_space* as) {
    // 9242_TODO error check instead
    seL4_Word dir_index = PT_TOP(user_ptr);
    seL4_Word page_index = PT_BOTTOM(user_ptr);
    assert(as->page_directory[dir_index] != NULL);
    seL4_Word frame_index = as->page_directory[dir_index][page_index];
    return index_to_vaddr(frame_index); 
}