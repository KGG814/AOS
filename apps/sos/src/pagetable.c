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
#include "syscalls.h"


#define FT_INDEX_MASK       0x000FFFFF
#define verbose 5

int handle_swap(seL4_Word vaddr, int pid, seL4_CPtr reply_cap);
void handle_swap_cb (int pid, seL4_CPtr reply_cap, void *args);
void copy_in_cb(int pid, seL4_CPtr reply_cap, void* args);
void copy_out_cb (int pid, seL4_CPtr reply_cap, void *args);
void sos_map_page_cb(int pid, seL4_CPtr reply_cap, void *args);
void sos_map_page_dir_cb(int pid, seL4_CPtr reply_cap, void *args);
void map_if_valid_cb (int pid, seL4_CPtr reply_cap, void *args);
void map_if_valid_cb_continue (int pid, seL4_CPtr reply_cap, void *args);
void copy_page_cb(int pid, seL4_CPtr, void *args);

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

void pt_cleanup(int pid) {
    for (int i = 0; i < CAP_TABLE_PAGES; i++) {
        
    }
} 

seL4_CPtr sos_map_page(int ft_index
                      ,seL4_Word vaddr
                      ,seL4_ARM_PageDirectory pd
                      ,addr_space* as
                      ,int pid
                      ) 
{
    sos_map_page_args *map_args = malloc(sizeof(sos_map_page_args));
    map_args->as = as;
    map_args->vaddr = vaddr;
    map_args->ft_index = ft_index;
    map_args->pd = pd;
    seL4_CPtr frame_cap;
    map_args->frame_cap = &frame_cap;
	seL4_Word dir_index = PT_TOP(vaddr);
    map_args->cb = NULL;
	/* Check that the page table exists */
    assert(as->page_directory != NULL);
	if (as->page_directory[dir_index] == NULL) {
        frame_alloc_args *args = malloc(sizeof(frame_alloc_args));
        args->map = KMAP;
        args->cb = sos_map_page_dir_cb;
        args->cb_args = (void *) map_args;
        frame_alloc_swap(pid, 0, args);
	} else {
        sos_map_page_cb(pid, 0, map_args); 
    }
    
    return frame_cap;
}

void sos_map_page_swap(int ft_index, seL4_Word vaddr, seL4_ARM_PageDirectory pd
                      ,addr_space* as, int pid, seL4_CPtr reply_cap
                      ,callback_ptr cb, void *cb_args
                      ,seL4_CPtr *frame_cap) {
    sos_map_page_args *map_args = malloc(sizeof(sos_map_page_args));
    map_args->as = as;
    map_args->vaddr = vaddr;
    map_args->ft_index = ft_index;
    map_args->pd = pd;
    map_args->frame_cap = frame_cap;
    map_args->cb = cb;
    map_args->cb_args = cb_args;
    seL4_Word dir_index = PT_TOP(vaddr);
    /* Check that the page table exists */
    assert(as->page_directory != NULL);
    if (as->page_directory[dir_index] == NULL) {
        frame_alloc_args *args = malloc(sizeof(frame_alloc_args));
        args->map = KMAP;
        args->cb = sos_map_page_dir_cb;
        args->cb_args = (void *) map_args;
        printf("sos_map_page_swap called frame_alloc_swap\n");
        frame_alloc_swap(pid, reply_cap, args);
    } else {
        sos_map_page_cb(pid, reply_cap, map_args); 
    }
}

void sos_map_page_dir_cb(int pid, seL4_CPtr reply_cap, void *args) {
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    sos_map_page_args *map_args = alloc_args->cb_args;
    seL4_Word dir_index = PT_TOP(map_args->vaddr);
    map_args->as->page_directory[dir_index] = (seL4_Word *) alloc_args->vaddr;
    free(alloc_args);
    sos_map_page_cb(pid, reply_cap, map_args);
}

void sos_map_page_cb(int pid, seL4_CPtr reply_cap, void *args) {
    sos_map_page_args *map_args = (sos_map_page_args *) args;
    addr_space *as = map_args->as;
    seL4_Word dir_index = PT_TOP(map_args->vaddr);
    seL4_Word page_index = PT_BOTTOM(map_args->vaddr);
    assert(as->page_directory[dir_index] != NULL);


    if ((as->page_directory[dir_index][page_index] & SWAPPED) == SWAPPED) {
        // 9242_TODO Swap things in
        // 9242_TODO get frame cap of swapped in page, preserve frame cap of
        // swapped out page
        //int slot = as->page_directory[dir_index][page_index] & SWAP_SLOT_MASK;
        frametable[map_args->ft_index].vaddr = map_args->vaddr;
    } else {
        as->page_directory[dir_index][page_index] = map_args->ft_index;
        as->page_directory[dir_index][page_index] |= pid << PROCESS_BIT_SHIFT;
        // Map into the given process page directory //
        *(map_args->frame_cap) = cspace_copy_cap(cur_cspace
                                   ,cur_cspace
                                   ,frametable[map_args->ft_index].frame_cap
                                   ,seL4_AllRights
                                   );
        map_page_user(*(map_args->frame_cap), map_args->pd, map_args->vaddr, 
                    seL4_AllRights, seL4_ARM_Default_VMAttributes, map_args->as);
        frametable[map_args->ft_index].vaddr = map_args->vaddr;
    }
    if (map_args->cb != NULL) {
        map_args->cb(pid, reply_cap, map_args->cb_args);
        free(map_args);
    }
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

    handle_swap(fault_vaddr, pid, reply_cap);
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

int map_if_valid(seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap) {
    int dir_index = PT_TOP(vaddr);
    int page_index = PT_BOTTOM(vaddr);
    
    if (proc_table[pid]->page_directory[dir_index] != NULL && 
        proc_table[pid]->page_directory[dir_index][page_index] != 0) {
        if (cb != NULL) {
            cb(pid, reply_cap, args);
        }
        return 0;
    }   
    int err = 0;
    int permissions = 0;
    if ((vaddr & PAGE_MASK) == GUARD_PAGE) {
        err = GUARD_PAGE_FAULT;
    } else if ((vaddr & PAGE_MASK) == 0) {
        err = NULL_DEREF;
    /* Stack pages*/
    } else if ((vaddr >= PROCESS_STACK_BOT) && (vaddr < PROCESS_STACK_TOP)) { 
    /* IPC Pages */
    } else if ((vaddr >= PROCESS_IPC_BUFFER) && (vaddr < PROCESS_IPC_BUFFER_END)) {;
    /* VMEM */
    } else if((vaddr >= PROCESS_VMEM_START) && (vaddr < proc_table[pid]->brk)) {
    /* Scratch */
    } else if ((vaddr >= PROCESS_SCRATCH)) {
    //} else if ((vaddr)) 
    } else {
      err = UNKNOWN_REGION;
    }
    if (err) {
        return err;
    }
    // map_if_valid args 
    map_if_valid_args *map_args = malloc(sizeof(map_if_valid_args));
    map_args->vaddr = vaddr;
    map_args->cb = cb;
    map_args->cb_args = args;

    // alloc_args
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    alloc_args->map = KMAP;
    alloc_args->cb = map_if_valid_cb;
    alloc_args->cb_args = (void *) map_args;
    frame_alloc_swap(pid, reply_cap, alloc_args);
    return 0;
}

void map_if_valid_cb (int pid, seL4_CPtr reply_cap, void *args) {
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    map_if_valid_args *map_args = alloc_args->cb_args;
    map_args->ft_index = alloc_args->index;
    seL4_CPtr temp;
    sos_map_page_swap(map_args->ft_index, map_args->vaddr, proc_table[pid]->vroot, 
                      proc_table[pid], pid, reply_cap, map_if_valid_cb_continue,
                      map_args, &temp);
    free(alloc_args);
}

void map_if_valid_cb_continue (int pid, seL4_CPtr reply_cap, void *args) {
    map_if_valid_args *map_args = (map_if_valid_args *)args;
    frametable[map_args->ft_index].vaddr = map_args->vaddr;
    map_args->cb(pid, reply_cap, map_args->cb_args);
    free(map_args);
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

int handle_swap(seL4_Word vaddr, int pid, seL4_CPtr reply_cap) {
    seL4_Word dir_index = PT_TOP(vaddr);
    seL4_Word page_index = PT_BOTTOM(vaddr);
    // If the page table does not exist, then it can't have been swapped out
    // also check if it has been swapped out
    if ((proc_table[pid]->page_directory[dir_index] != NULL) 
    && (proc_table[pid]->page_directory[dir_index][page_index] & SWAPPED)) {       
        // If it has been swapped out, get swap file offset from page table entry and swap it back in. 
        int swap_offset = (proc_table[pid]->page_directory[dir_index][page_index] & SWAP_SLOT_MASK) * PAGE_SIZE;
        // Map the page in. If it is necessary, a frame will be swapped out to make space by frame_alloc
        int err = map_if_valid(vaddr, pid, handle_swap_cb, NULL, reply_cap);
        if (err) {
            return err;
        }
        
    } 
    
    // No swapping to be done, continue as normal
    return 0;
}

void handle_swap_cb (int pid, seL4_CPtr reply_cap, void *args) {
    // Get the frame for the page that was mapped in
    //int index = proc_table[pid]->page_directory[dir_index][page_index] & FT_INDEX_MASK;
    // Get the kernel mapping for that frame
    //seL4_Word k_vaddr = index_to_vaddr(index);
    // 9242_TODO Do a NFS read from the swap file to the addr
}

void copy_in(int pid, seL4_CPtr reply_cap, copy_in_args *args) {
    copy_in_args *copy_args = (copy_in_args *) args;
    if (copy_args->count == args->nbyte) {
        copy_args->cb(pid, reply_cap, args);
    } else {
        int err = map_if_valid(copy_args->usr_ptr & PAGE_MASK, pid, copy_in_cb, args, reply_cap);
        if (err) {
            send_seL4_reply(reply_cap, copy_args->count);
        }
    }
}

void copy_in_cb(int pid, seL4_CPtr reply_cap, void *args) {
    //9242_TODO pin the page
    copy_in_args *copy_args = args;
    int to_copy = copy_args->nbyte - copy_args->count;
    if ((copy_args->usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
        to_copy = PAGE_SIZE - (copy_args->usr_ptr & ~PAGE_MASK);
    } 
    seL4_Word src = user_to_kernel_ptr(copy_args->usr_ptr, pid);
    memcpy((void *) (copy_args->k_ptr), (void *) src, to_copy);
    copy_args->count += to_copy;
    copy_args->usr_ptr += to_copy;
    copy_args->k_ptr += to_copy;
    copy_in(pid, reply_cap, args);
}

//copy from kernel ptr to usr ptr 
void copy_out(int pid, seL4_CPtr reply_cap, copy_out_args* args) {
    if (args->count == args->nbyte) {
        args->cb(pid, reply_cap, args);
    } else {
        int to_copy = args->nbyte - args->count;
        if ((args->usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
            to_copy = PAGE_SIZE - (args->usr_ptr & ~PAGE_MASK);
        } 
        int err = copy_page(args->usr_ptr, to_copy, args->src, pid, copy_out_cb, args, reply_cap);
        if (err) {
            send_seL4_reply(reply_cap, args->count);
        }
    }

} 

void copy_out_cb (int pid, seL4_CPtr reply_cap, void *args) {
    copy_out_args *copy_args = args;
    int to_copy = copy_args->nbyte - copy_args->count;
    if ((copy_args->usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
        to_copy = PAGE_SIZE - (copy_args->usr_ptr & ~PAGE_MASK);
    }
    copy_args->count += to_copy;
    copy_args->usr_ptr += to_copy;
    copy_args->src += to_copy;
    copy_out(pid, reply_cap, args);
}

int copy_page(seL4_Word dst
             ,int count
             ,seL4_Word src
             ,int pid
             ,callback_ptr cb
             ,void *cb_args
             ,seL4_CPtr reply_cap
             ) 
{
    copy_page_args *copy_args = malloc(sizeof(copy_page_args));
    copy_args->dst = dst;
    copy_args->src = src;
    copy_args->count = count;
    copy_args->cb = cb;
    copy_args->cb_args = cb_args;
    int err = map_if_valid(dst & PAGE_MASK, pid, copy_page_cb, copy_args, reply_cap);
	//9242_TODO pin the page
    if (err) {
        return err;
    }
    return 0;
}

void copy_page_cb(int pid, seL4_CPtr reply_cap, void *args) {
    copy_page_args *copy_args = (copy_page_args *) args;
    seL4_Word kptr = user_to_kernel_ptr(copy_args->dst, pid);
    memcpy((void *)kptr, (void *)copy_args->src , copy_args->count);
    copy_args->cb(pid, reply_cap, copy_args->cb_args);
    free(args);
}
