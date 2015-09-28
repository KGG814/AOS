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
#include <string.h>


#define FT_INDEX_MASK       0x000FFFFF
#define verbose 5

int handle_swap(seL4_Word vaddr, int pid);

int page_init(int pid) {
    seL4_Word vaddr;
    frame_alloc(&vaddr, KMAP, pid);
    proc_table[pid]->page_directory = (seL4_Word**) vaddr;
    // 9242_TODO Set to no swap
    for (int i = 0; i < CAP_TABLE_PAGES; i++) {
        frame_alloc(&vaddr, KMAP, pid);
        proc_table[pid]->cap_table[i] = (seL4_ARM_PageTable*)vaddr;
    }

    return 0;
}

seL4_CPtr sos_map_page(int ft_index
                      ,seL4_Word vaddr
                      ,seL4_ARM_PageDirectory pd
                      ,addr_space* as
                      ,int pid
                      ) 
{
	seL4_Word dir_index = PT_TOP(vaddr);
	seL4_Word page_index = PT_BOTTOM(vaddr);
	/* Check that the page table exists */
    assert(as->page_directory != NULL);
    int index = 0;
    seL4_Word temp;
	if (as->page_directory[dir_index] == NULL) {
        printf("sos_map_page calling frame_alloc\n");
        index = frame_alloc(&temp, KMAP, pid);
        as->page_directory[dir_index] = (seL4_Word*)temp;
        assert(index > FRAMETABLE_OK);
        //9242_TODO Set to no swap
	}
	/* Map into the sos page table 
       ft_index is the lower 20 bits */
    assert(as->page_directory[dir_index] != NULL);
    seL4_CPtr frame_cap;
    if ((as->page_directory[dir_index][page_index] & SWAPPED) == SWAPPED) {
        // 9242_TODO Swap things in
		// 9242_TODO get frame cap of swapped in page, preserve frame cap of
        // swapped out page
        //int slot = as->page_directory[dir_index][page_index] & SWAP_SLOT_MASK;
        frametable[index].vaddr = vaddr;
    } else {
        as->page_directory[dir_index][page_index] = ft_index;
        as->page_directory[dir_index][page_index] |= pid << PROCESS_BIT_SHIFT;
        // Map into the given process page directory //
        frame_cap = cspace_copy_cap(cur_cspace
                                   ,cur_cspace
                                   ,frametable[ft_index].frame_cap
                                   ,seL4_AllRights
                                   );

        map_page_user(frame_cap, pd, vaddr, 
                    seL4_AllRights, seL4_ARM_Default_VMAttributes, as);
        frametable[index].vaddr = vaddr;
    }
    return frame_cap;
}

void handle_vm_fault(seL4_Word badge, int pid) {
    // 9242_TODO Kill process if invalid memory 
    // 9242_TODO Instruction faults?
    seL4_CPtr reply_cap;
    seL4_Word fault_vaddr = seL4_GetMR(1);
    // Get the page of the fault address
    fault_vaddr &= PAGE_MASK;
    //dprintf(0, "Handling fault at: 0x%08x\n", fault_vaddr);
    reply_cap = cspace_save_reply_cap(cur_cspace);
    handle_swap(fault_vaddr, pid);
    printf("handle_vm_fault calling map_if_valid\n");
    int err = map_if_valid(fault_vaddr, pid, handle_vm_fault_cb, NULL, reply_cap);
    if (err == GUARD_PAGE_FAULT || err == UNKNOWN_REGION || err == NULL_DEREF) {
        // 9242_TODO Kill process
    }
}

void handle_vm_fault_cb(int pid, seL4_CPtr cap, void* args) {
    /* Reply */
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, 0);
    seL4_Send(cap, reply);
    cspace_free_slot(cur_cspace, cap);
}


seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, int pid) {
    // 9242_TODO error check instead
    seL4_Word dir_index = PT_TOP(user_ptr);
    seL4_Word page_index = PT_BOTTOM(user_ptr);
    assert(proc_table[pid]->page_directory[dir_index] != NULL);
    seL4_Word frame_index = proc_table[pid]->page_directory[dir_index][page_index];
    return index_to_vaddr(frame_index) + (user_ptr & ~(PAGE_MASK)); 
}

void user_buffer_map(seL4_Word user_ptr, size_t nbyte, int pid) {
    seL4_Word start_page = user_ptr & PAGE_MASK;
    seL4_Word end_page = (user_ptr + nbyte) & PAGE_MASK;
    for (seL4_Word curr_page = start_page; curr_page <= end_page; curr_page += PAGE_SIZE) {
        int pt_top = PT_TOP(curr_page);
        int pt_bot = PT_BOTTOM(curr_page);
        //9242_TODO: change this to check if it was swapped out as well
        if (proc_table[pid]->page_directory[pt_top] != NULL && 
            proc_table[pid]->page_directory[pt_top][pt_bot] != 0) {
            continue;
        }
        printf("user_buffer_map calling map_if_valid\n");
        map_if_valid(curr_page, pid, NULL, NULL, 0);
    }
}

int map_if_valid(seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap) {
    seL4_Word page_vaddr;
    printf("map_if_valid calling frame_alloc\n");
    int ft_index = frame_alloc(&page_vaddr, KMAP, pid);
    assert(ft_index > FRAMETABLE_OK);
    int err = 0;
    int dir_index = PT_TOP(vaddr);
    int page_index = PT_BOTTOM(vaddr);
    if (proc_table[pid]->page_directory[dir_index] != NULL && 
        proc_table[pid]->page_directory[dir_index][page_index] != 0) {
        printf("map_if_valid calling frame_free\n");
        frame_free(ft_index);
        return 0;
    }
    if ((vaddr & PAGE_MASK) == GUARD_PAGE) {
        /* Kill process */
        return GUARD_PAGE_FAULT;
    } else if ((vaddr & PAGE_MASK) == 0) {
        err = NULL_DEREF;
        frame_free(ft_index);
    /* Stack pages*/
    } else if ((vaddr >= PROCESS_STACK_BOT) && (vaddr < PROCESS_STACK_TOP)) {
        printf("map_if_valid calling sos_map_page\n");
        sos_map_page(ft_index, vaddr, proc_table[pid]->vroot, proc_table[pid], pid);
    /* IPC Pages */
    } else if ((vaddr >= PROCESS_IPC_BUFFER) && (vaddr < PROCESS_IPC_BUFFER_END)) {
        printf("map_if_valid calling sos_map_page\n");
        sos_map_page(ft_index, vaddr, proc_table[pid]->vroot, proc_table[pid], pid);
    /* VMEM */
    } else if((vaddr >= PROCESS_VMEM_START) && (vaddr < proc_table[pid]->brk)) {
        printf("map_if_valid calling sos_map_page\n");
        sos_map_page(ft_index, vaddr, proc_table[pid]->vroot, proc_table[pid], pid);
    /* Scratch */
    } else if ((vaddr >= PROCESS_SCRATCH)) {
        printf("map_if_valid calling sos_map_page\n");
        sos_map_page(ft_index, vaddr, proc_table[pid]->vroot, proc_table[pid], pid); 
    //} else if ((vaddr)) 
    } else {
      err = UNKNOWN_REGION;
      frame_free(ft_index);
    }
    if (err) {
        dprintf(0, "Address %p was not in valid region\n", vaddr);
        return UNKNOWN_REGION;
    }
    frametable[ft_index].vaddr = vaddr;
    if (cb != NULL) {
        cb(pid, reply_cap, args);
    }
    return 0;
}

int check_region(seL4_Word start, seL4_Word size) {
    for (seL4_Word curr = start; curr < start + size; curr += PAGE_SIZE) {
        if ((curr & PAGE_MASK) == GUARD_PAGE) {
            return EFAULT;
        } else if ((curr & PAGE_MASK) == 0) {
            return EFAULT;
        } else if ((curr >= PROCESS_STACK_TOP) && (curr < PROCESS_IPC_BUFFER)) {
            return EFAULT;
        } else if((curr >= PROCESS_IPC_BUFFER_END) && (curr < PROCESS_SCRATCH)) {
            return EFAULT;
        }
    }
    return 0;
}

int handle_swap(seL4_Word vaddr, int pid) {
    seL4_Word dir_index = PT_TOP(vaddr);
    seL4_Word page_index = PT_BOTTOM(vaddr);
    // If the page table does not exist, then it can't have been swapped out
    // also check if it has been swapped out
    if ((proc_table[pid]->page_directory[dir_index] != NULL) 
    && (proc_table[pid]->page_directory[dir_index][page_index] & SWAPPED)) {       
        // If it has been swapped out, get swap file offset from page table entry and swap it back in. 
        int swap_offset = (proc_table[pid]->page_directory[dir_index][page_index] & SWAP_SLOT_MASK) * PAGE_SIZE;
        // Map the page in. If it is necessary, a frame will be swapped out to make space by frame_alloc
        printf("handle_swap calling map_if_valid\n");
        int err = map_if_valid(vaddr, pid, NULL, NULL, 0);
        if (err) {
            return err;
        }
        // Get the frame for the page that was mapped in
        //int index = proc_table[pid]->page_directory[dir_index][page_index] & FT_INDEX_MASK;
        // Get the kernel mapping for that frame
        //seL4_Word k_vaddr = index_to_vaddr(index);
        // 9242_TODO Do a NFS read from the swap file to the addr
    } 
    
    // No swapping to be done, continue as normal
    return 0;
}

int copy_in(seL4_Word usr_ptr
           ,seL4_Word k_ptr
           ,int nbyte
           ,int pid 
           ) {
    int count = 0;
    while (count != nbyte) {
        int to_copy = nbyte - count;
        if ((usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
            to_copy = PAGE_SIZE - (usr_ptr & ~PAGE_MASK);
        } 
        int err = map_if_valid(usr_ptr & PAGE_MASK, pid, NULL, NULL, 0);
        if (err) {
            return count;
        }
        //9242_TODO pin the page
        seL4_Word src = user_to_kernel_ptr(usr_ptr, pid);
        memcpy((void *) k_ptr, (void *) src, to_copy);

        count += to_copy;
        usr_ptr += to_copy;
        k_ptr += to_copy;
    } 
    return count;

}

//copy from kernel ptr to usr ptr 
int copy_out(seL4_Word usr_ptr
           ,seL4_Word src
           ,int nbyte
           ,int pid 
           )
{
    int count = 0; 
    while (count != nbyte) {
        int to_copy = nbyte - count;
        if ((usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
            to_copy = PAGE_SIZE - (usr_ptr & ~PAGE_MASK);
        } 
        if (!copy_page(usr_ptr, to_copy, src, pid)) {
            return count;
        }
        count += to_copy;
        usr_ptr += to_copy;
        src += to_copy;
    } 
    return count;
} 

int copy_page(seL4_Word dst
             ,int count
             ,seL4_Word src
             ,int pid
             ) 
{
    printf("copy_page calling map_if_valid\n");
    int err = map_if_valid(dst & PAGE_MASK, pid, NULL, NULL, 0);
	//9242_TODO pin the page
    if (err) {
        return err;
    }
    seL4_Word kptr = user_to_kernel_ptr(dst, pid);
    memcpy((void *)kptr, (void *)src , count);
    //9242_TODO unpin the page
    return 0;
}
