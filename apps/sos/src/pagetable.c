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
#include "swap.h"
#include "debug.h"
#define FT_INDEX_MASK       0x000FFFFF
#define verbose 5

void copy_in_cb(int pid, seL4_CPtr reply_cap, void* args, int err);
void copy_out_cb (int pid, seL4_CPtr reply_cap, void *args, int err);
void sos_map_page_cb(int pid, seL4_CPtr reply_cap, void *args, int err);
void sos_map_page_dir_cb(int pid, seL4_CPtr reply_cap, void *args, int err);
void map_if_valid_cb (int pid, seL4_CPtr reply_cap, void *args, int err);
void map_if_valid_cb_continue (int pid, seL4_CPtr reply_cap, void *args, int err);
void copy_page_cb(int pid, seL4_CPtr, void *args, int err);
void pd_init_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args, int err);
void pd_caps_init(int pid, seL4_CPtr reply_cap, vm_init_args *vm_args);
void pd_caps_init_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args, int err);
int map_new_frame (seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap);

// Call to initialise the SOS page directory, as well as the 
// frames to store caps for the ARM page tables
void vm_init(int pid, seL4_CPtr reply_cap, vm_init_args *args) {
    if (SOS_DEBUG) printf("vm_init\n");
    // Set up frame frame_alloc args
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));

    if (alloc_args == NULL) {
        eprintf("Error caught in vm_init\n");
        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    alloc_args->map     = KMAP;
    alloc_args->swap    = NOT_SWAPPABLE;
    alloc_args->cb      = (callback_ptr) pd_init_cb;
    alloc_args->cb_args = args;
    // Get a frame for the page directory
    frame_alloc_swap(pid, reply_cap, alloc_args, 0);
    if (SOS_DEBUG) printf("vm_init ended\n");
}

// Callback for SOS page directory setup
void pd_init_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args, int err) {
    if (SOS_DEBUG) printf("pd_init_cb\n");
    // Get the return values from the frame_alloc call
    vm_init_args *vm_args  = (vm_init_args *) args->cb_args;
    int index                   = args->index;
    seL4_Word vaddr             = args->vaddr;

    if (index == FRAMETABLE_ERR || err) {
        eprintf("Error caught in pd_init_cb\n");
        vm_args->cb(pid, reply_cap, vm_args->cb_args, -1);
        free(args);
        free(vm_args);
        return;
    }

    // Set up args for callback
    vm_args->curr_page = 0;
    // Free args from frame_alloc cal
    free(args);
    // Set the page directory to the newly allocated page
    proc_table[pid]->page_directory = (seL4_Word**) vaddr;
    memset((void *)vaddr, 0, PAGE_SIZE);

    if (frametable[index].mapping_cap) {
       seL4_ARM_Page_Unify_Instruction(frametable[index].mapping_cap, 0, PAGESIZE); 
    }
    
    if (TMP_DEBUG) printf("pd addr %p\n",  proc_table[pid]->page_directory[0]);

    // Make sure the page directory isn't swapped out
    printf("Setting index %p to don't swap\n", (void *) index);
    frametable[index].frame_status |= FRAME_DONT_SWAP;

    // Continue initialisation
    pd_caps_init(pid, reply_cap, vm_args);
    if (SOS_DEBUG) printf("pd_init_cb ended\n");
}

// Initialisation for ARM page table cap storage frames
void pd_caps_init(int pid, seL4_CPtr reply_cap, vm_init_args *args) {
    if (SOS_DEBUG) printf("pd_caps_init\n");

    // Get arguments we need
    // loop counter for allocating pages
    int curr_page = args->curr_page;
    // Check if we have allocated all the pages
    if (curr_page < CAP_TABLE_PAGES) {
        // Haven't allocated pages yet, call frame_alloc
        // And loop back to this function through
        // the callback
        // Set up frame_alloc args
        printf("curr page %d, max pages %d\n", curr_page, CAP_TABLE_PAGES);
        frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
        if (alloc_args == NULL) {
            eprintf("Error caught in pd_caps_init\n");
            args->cb(pid, reply_cap, args->cb_args, -1);
            free(args);
            return;
        }

        alloc_args->map     = KMAP;
        alloc_args->swap    = NOT_SWAPPABLE;
        alloc_args->cb      = (callback_ptr) pd_caps_init_cb;
        alloc_args->cb_args = args;
        frame_alloc_swap(pid, reply_cap, alloc_args, 0);
    } else {
        // Allocated all the frames, do callback
        printf("curr_page %d, max pages, %d\n",curr_page, CAP_TABLE_PAGES);
        args->cb(pid, reply_cap, args->cb_args, 0);
        free(args);
    }
    if (SOS_DEBUG) printf("pd_caps_init ended\n");
}

void pd_caps_init_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args, int err) {
    if (SOS_DEBUG) printf("pd_caps_init_cb\n");
    // Get the return values from the frame_alloc call
    // Kernel vaddr of frame that was mapped
    seL4_Word vaddr = args->vaddr;
    // Frametable index of frame that was mapped
    seL4_Word index = args->index;
    // Arguments for this function call
    vm_init_args *vm_args = (vm_init_args *) args->cb_args;
    printf("Args at %p\n", vm_args);
    // Free frame_alloc args
    free(args);

    if (err || index == FRAMETABLE_ERR) {
        eprintf("Error caught in pd_caps_init_cb\n");
        vm_args->cb(pid, reply_cap, vm_args->cb_args, -1);
        free(vm_args);
        return;
    }

    // Get arguments we need
    // loop counter
    memset((void *)vaddr, 0, PAGE_SIZE);
    
    //this always seems to be the case
    //if (0) {//!frametable[index].mapping_cap) {
        //eprintf("Before ccc\n");
        //seL4_CPtr cap =  cspace_copy_cap(cur_cspace
        //                                ,cur_cspace
        //                                ,frametable[index].frame_cap
        //                                ,seL4_AllRights
        //                                );
        //eprintf("after ccc\n");
        //frametable[index].mapping_cap = cap;
    //}
    //assert(frametable[index].mapping_cap);
    //9242_TODO this never happens properly
    //eprintf("Before flush\n");
    //seL4_ARM_Page_Unify_Instruction(frametable[index].mapping_cap, 0, PAGESIZE);
    //eprintf("After flush\n");

    int curr_page = vm_args->curr_page;
    // Set cap storage to the frame we just allocated
    // This is an array of ARM page tables
    printf("pid %d, curr_page %d\n", pid, curr_page);
    proc_table[pid]->cap_table[curr_page] = (seL4_ARM_PageTable*) vaddr;
    // Don't swap these frames
    printf("Setting index %p to don't swap\n", (void *) index);
    frametable[index].frame_status |= FRAME_DONT_SWAP;
    // Increment loop counter
    vm_args->curr_page++;
    // Call to start of loop
    pd_caps_init(pid, reply_cap, vm_args);
    if (SOS_DEBUG) printf("pd_caps_init_cb ended\n");
}

void pt_cleanup(int pid) {
    assert(pid > 0 && pid <= MAX_PROCESSES);
    //free all the pages + revoke/delete the corresponding cap 
    //9242_TODO unmark swapped out pages
    printf("Starting pt_cleanup\n");
    seL4_Word** pd = proc_table[pid]->page_directory;
    seL4_ARM_PageTable **ct = proc_table[pid]->cap_table;
    if (pd) {
        for (int i = 0; i < PD_MAX_ENTRIES; i++) {
            if (pd[i]) {
                for (int j = 0; j < PT_MAX_ENTRIES; j++) {
                    if (pd[i][j]) {
                        if (pd[i][j] & SWAPPED) {
                            printf("freeing a swap slot: %d\n", pd[i][j] & SWAP_SLOT_MASK);
                            free_swap_slot(pd[i][j] & SWAP_SLOT_MASK); 
                        } else {
                            int index = pd[i][j] & FRAME_INDEX_MASK;
                            int page_pid = (frametable[index].frame_status & PROCESS_MASK) >> PROCESS_BIT_SHIFT;
                            //if (page_pid == pid) {
                            printf("page_pid = %d, pid = %d\n", page_pid, pid);
                            frame_free(index);
                            //} 
                        }
                    }
                }
                frame_free(vaddr_to_index((seL4_Word) pd[i]));
            }
        }
        frame_free(vaddr_to_index((seL4_Word) pd));
    }

    //free the cap table 
    if (ct) {
        for (int i = 0; i < CAP_TABLE_PAGES; i++) {
            for (int j = 0; j < CT_MAX_ENTRIES; j++) {
                if ((seL4_Word) ct[i][j]) {
                    seL4_ARM_Page_Unmap(ct[i][j]);
                }
            }
            frame_free(vaddr_to_index((seL4_Word) ct[i])); 
        }
    }
    printf("pt cleanup ended\n");
} 

void sos_map_page_swap(int ft_index, seL4_Word vaddr, int pid, seL4_CPtr reply_cap
                      ,callback_ptr cb, void *cb_args) {
    if (SOS_DEBUG) printf("sos_map_page_swap, %p\n", (void *) vaddr);
    sos_map_page_args *map_args = malloc(sizeof(sos_map_page_args));
    if (map_args == NULL) {
        eprintf("Error caught in sos_map_page_swap\n");
        cb(pid, reply_cap, cb_args, -1);
        return;
    }

    addr_space *as = proc_table[pid];
    map_args->vaddr = vaddr & PAGE_MASK;
    map_args->ft_index = ft_index;
    map_args->cb = cb;
    map_args->cb_args = cb_args;
    seL4_Word dir_index = PT_TOP(vaddr);
    /* Check that the page table exists */
    assert(as->page_directory != NULL);
    if (as->page_directory[dir_index] == NULL) {
        printf("Making page directory\n");
        frame_alloc_args *args = malloc(sizeof(frame_alloc_args));
        if (args == NULL) {
            eprintf("Error caught in sos_map_page_swap\n");
            cb(pid, reply_cap, cb_args, -1);
            free(map_args);
            return;
        } else {
            args->map = KMAP;
            args->swap = SWAPPABLE;
            args->cb = sos_map_page_dir_cb;
            args->cb_args = (void *) map_args;
            frame_alloc_swap(pid, reply_cap, args, 0);
        }
        
    } else {
        sos_map_page_cb(pid, reply_cap, map_args, 0); 
    }
    if (SOS_DEBUG) printf("sos_map_page_swap end\n");
}

void sos_map_page_dir_cb(int pid, seL4_CPtr reply_cap, void *args, int err) {
    if (SOS_DEBUG) printf("sos_map_page_dir_cb\n");

    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    sos_map_page_args *map_args = alloc_args->cb_args;

    if (err || !alloc_args->index) {
        eprintf("Error caught in sos_map_page_dir_cb\n");
        free(alloc_args);
        map_args->cb(pid, reply_cap, map_args->cb_args, -1);
        free(map_args);
        return;
    }

    seL4_Word dir_index = PT_TOP(map_args->vaddr);

    printf("directory index %p, pagetable addr %p\n",(void *)dir_index, (void *)alloc_args->vaddr);
    printf("index %d\n", alloc_args->index);

    proc_table[pid]->page_directory[dir_index] = (seL4_Word *) alloc_args->vaddr;

    seL4_ARM_Page_Unmap(frametable[alloc_args->index].frame_cap);
    err = map_page(frametable[alloc_args->index].frame_cap
                  ,seL4_CapInitThreadPD
                  ,alloc_args->vaddr
                  ,seL4_AllRights
                  ,seL4_ARM_Default_VMAttributes
                  );

    if (err) {
        eprintf("Error caught in sos_map_page_dir_cb\n");
        free(alloc_args);
        map_args->cb(pid, reply_cap, map_args->cb_args, -1);
        free(map_args);
        return;
    }

    memset((void *)alloc_args->vaddr, 0, PAGE_SIZE);

    frametable[alloc_args->index].vaddr = -1;
    printf("Setting index %p to don't swap\n", (void *)alloc_args->index);
    frametable[alloc_args->index].frame_status |= FRAME_DONT_SWAP;
    free(alloc_args);

    sos_map_page_cb(pid, reply_cap, map_args, 0);

    if (SOS_DEBUG) printf("sos_map_page_dir_cb ended\n");
}

void sos_map_page_cb(int pid, seL4_CPtr reply_cap, void *args, int err) {
    // Get arguments we need 
    sos_map_page_args *map_args = (sos_map_page_args *) args;
    int index = map_args->ft_index;

    if (err || index == FRAMETABLE_ERR) {
        eprintf("Error caught in sos_map_page_cb\n");
        map_args->cb(pid, reply_cap, map_args->cb_args, -1);
        free(args);
        return;
    }

    addr_space *as = proc_table[pid];
    seL4_ARM_PageDirectory pd = as->vroot;
    seL4_Word vaddr = map_args->vaddr;
    
    if (SOS_DEBUG) printf("sos_map_page_cb at %p\n", (void *) map_args->vaddr);

    seL4_Word dir_index = PT_TOP(vaddr);
    seL4_Word page_index = PT_BOTTOM(vaddr);

    assert(as->page_directory[dir_index] != NULL);

    if (SOS_DEBUG) printf("dir_index %d, page_index %d\n", dir_index, page_index);

    if ((as->page_directory[dir_index][page_index] & SWAPPED) == SWAPPED) {
        printf("Page was swapped out from under us: %p, pid: %d, value %p\n", (void *) vaddr, pid, 
                (void *) as->page_directory[dir_index][page_index]);
        assert(0);
    } else {
        as->page_directory[dir_index][page_index] = index;
        // Map into the given process page directory //
        if (!frametable[index].mapping_cap) {
            seL4_CPtr cap = cspace_copy_cap(cur_cspace
                                   ,cur_cspace
                                   ,frametable[index].frame_cap
                                   ,seL4_AllRights
                                   );
            err = map_page_user(cap, pd, vaddr, 
                        seL4_AllRights, seL4_ARM_Default_VMAttributes, as);

            if (err) {
                eprintf("Error caught in sos_map_page_cb\n");
                map_args->cb(pid, reply_cap, map_args->cb_args, -1);
                free(args);
                return;
            }

            seL4_ARM_Page_Unify_Instruction(cap, 0, PAGESIZE); 
            frametable[index].mapping_cap = cap; 
            if (SOS_DEBUG) printf("setting mapping cap: %d\n", cap);
            frametable[map_args->ft_index].vaddr = map_args->vaddr;
        }     
    }
    seL4_ARM_Page_Unify_Instruction(frametable[index].mapping_cap, 0, PAGESIZE);
    if (map_args->cb != NULL) {
        map_args->cb(pid, reply_cap, map_args->cb_args, 0);
        free(map_args);
    }
    if (SOS_DEBUG) printf("sos_map_page_cb ended\n");
}

void handle_vm_fault(seL4_Word badge, int pid) {
    
    seL4_CPtr reply_cap;
    seL4_Word fault_vaddr = seL4_GetMR(1);
    if (SOS_DEBUG) printf("handle_vm_fault, %p\n", (void *) fault_vaddr);
    // Get the page of the fault address
    fault_vaddr &= PAGE_MASK;
    //dprintf(0, "Handling fault at: 0x%08x\n", fault_vaddr);
    reply_cap = cspace_save_reply_cap(cur_cspace);

    int err = map_if_valid(fault_vaddr, pid, handle_vm_fault_cb, NULL, reply_cap);
    if (err == GUARD_PAGE_FAULT || err == UNKNOWN_REGION || err == NULL_DEREF) {

        //kill the process as it has faulted invalid memory
        kill_process(pid, pid, reply_cap);
    }
    if (SOS_DEBUG) printf("handle_vm_fault finished\n");
}

void handle_vm_fault_cb(int pid, seL4_CPtr cap, void* args, int err) {
    /* Reply */
    if (SOS_DEBUG) printf("handle_vm_fault_cb\n");
    if (err) {
        //this could happen. in this case the process needs to be killed 
        kill_process(pid, pid, cap);
        return;
    }

    send_seL4_reply(cap, pid, 0);
    if (SOS_DEBUG) printf("handle_vm_fault_cb ended\n");
}


seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, int pid) {
    seL4_Word dir_index = PT_TOP(user_ptr);
    seL4_Word page_index = PT_BOTTOM(user_ptr);
    assert(proc_table[pid]->page_directory[dir_index] != NULL);
    seL4_Word frame_index = proc_table[pid]->page_directory[dir_index][page_index];
    return index_to_vaddr(frame_index) + (user_ptr & ~(PAGE_MASK)); 
}

int map_if_valid(seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap) {
    if (SOS_DEBUG) printf("map_if_valid: %p, pid = %d\n", (void * ) vaddr, pid);
    int dir_index = PT_TOP(vaddr);
    int page_index = PT_BOTTOM(vaddr);

    printf("pd addr = %p\n", proc_table[pid]->page_directory);
    if (proc_table[pid]->page_directory == NULL) {
        //assert(0);
        //cb(pid, reply_cap, args, -1);
        eprintf("Error caught in map_if_valid\n");
        return -1;
    }
    
    if (proc_table[pid]->page_directory[dir_index] != NULL) {
        int index = proc_table[pid]->page_directory[dir_index][page_index];
        if (index & SWAPPED) {
            if (SOS_DEBUG) printf("page was swapped\n");
            read_swap_args *swap_args = malloc(sizeof(read_swap_args));
            if (swap_args == NULL) {
                eprintf("Error caught in map_if_valid\n");
                return -1;
            }
            swap_args->cb = cb;
            swap_args->cb_args = args;
            swap_args->vaddr = vaddr;
            swap_args->pid = pid = pid;
            swap_args->reply_cap = reply_cap;
            read_from_swap_slot(pid, reply_cap, swap_args);
            return 0;
        } else if (index != 0 && frametable[index].vaddr == vaddr && 
                   frametable[index].frame_status & FRAME_SWAP_MARKED) {
            // Was temporarily unmapped by clock algo, just map back in
            if (SOS_DEBUG) printf("unmapped by clock, map back in\n");
            seL4_CPtr cap = cspace_copy_cap(cur_cspace
                                   ,cur_cspace
                                   ,frametable[index].frame_cap
                                   ,seL4_AllRights
                                   );
            int err = map_page_user(cap, proc_table[pid]->vroot, vaddr, 
                    seL4_AllRights, seL4_ARM_Default_VMAttributes, proc_table[pid]);
            if (err) {
                eprintf("Error caught in map_if_valid\n");
                return -1;
            }

            frametable[index].frame_status &= ~FRAME_SWAP_MARKED;
            seL4_ARM_Page_Unify_Instruction(cap, 0, PAGESIZE);
            frametable[index].mapping_cap = cap;
            if (cb != NULL) {
                cb(pid, reply_cap, args, 0);
                return 0;
            }
        } else if (index != 0) {
            if (SOS_DEBUG) printf("page should be mapped in\n");
            if (cb != NULL) {
                cb(pid, reply_cap, args, 0);
                return 0;
            }
        }
    }    

    return map_new_frame(vaddr, pid, cb, args, reply_cap);
}

int map_new_frame (seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap) {
    int err = 0;
    //int permissions = 0;
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
    if (map_args == NULL) {
        eprintf("Error caught in map_new_frame\n");
        return -1;
    }
    map_args->vaddr = vaddr;
    map_args->cb = cb;
    map_args->cb_args = args;

    // alloc_args
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    if (alloc_args == NULL) {
        eprintf("Error caught in map_new_frame\n");
        free(map_args);
        return -1;
    }
    alloc_args->map = KMAP;
    alloc_args->swap = SWAPPABLE;
    alloc_args->cb = map_if_valid_cb;
    alloc_args->cb_args = (void *) map_args;
    frame_alloc_swap(pid, reply_cap, alloc_args, 0);
    return 0;
}

void map_if_valid_cb (int pid, seL4_CPtr reply_cap, void *args, int err) {
    if (SOS_DEBUG) printf("map_if_valid_cb\n");
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    map_if_valid_args *map_args = alloc_args->cb_args;
    map_args->ft_index = alloc_args->index;

    if (err || alloc_args->index == FRAMETABLE_ERR) {
        eprintf("Error caught in map_if_valid_cb\n");
        map_args->cb(pid, reply_cap, map_args->cb_args, -1);
        free(map_args);
        free(alloc_args);
        return;
    }

    sos_map_page_swap(map_args->ft_index
                     ,map_args->vaddr
                     ,pid
                     ,reply_cap
                     ,map_if_valid_cb_continue
                     ,map_args
                     );
    free(alloc_args);
    if (SOS_DEBUG) printf("map_if_valid_cb ended\n");
}

void map_if_valid_cb_continue (int pid, seL4_CPtr reply_cap, void *args, int err) {
    if (SOS_DEBUG) printf("map_if_valid_cb_continue\n");
    map_if_valid_args *map_args = (map_if_valid_args *)args;
    frametable[map_args->ft_index].vaddr = map_args->vaddr;
    map_args->cb(pid, reply_cap, map_args->cb_args, err);
    free(map_args);
    if (SOS_DEBUG) printf("map_if_valid_cb_continue ended\n");
}

int check_region(int pid, seL4_Word start, seL4_Word size) {
    for (seL4_Word vaddr = start; vaddr < start + size; vaddr += PAGE_SIZE) {
        
        if ((vaddr & PAGE_MASK) == GUARD_PAGE) {
            return -1;
        } else if ((vaddr & PAGE_MASK) == 0) {
            return -1;
        } else if ((vaddr >= PROCESS_STACK_TOP) && (vaddr < PROCESS_IPC_BUFFER)) { 
            return -1;
        } else if ((vaddr >= PROCESS_IPC_BUFFER_END) && (vaddr < PROCESS_SCRATCH)) { 
            return -1;
        }
        /*
        if ((vaddr & PAGE_MASK) == GUARD_PAGE) {
            return EFAULT;
        } else if ((vaddr & PAGE_MASK) == 0) {
            return EFAULT;
            // Stack pages
        } else if ((vaddr >= PROCESS_STACK_BOT) && (vaddr < PROCESS_STACK_TOP)) { 
            // IPC Pages 
        } else if ((vaddr >= PROCESS_IPC_BUFFER) && (vaddr < PROCESS_IPC_BUFFER_END)) {;
            // VMEM 
        } else if((vaddr >= PROCESS_VMEM_START) && (vaddr < proc_table[pid]->brk)) {
            // Scratch 
        } else if ((vaddr >= PROCESS_SCRATCH)) {
            //} else if ((vaddr)) 
        } else {
            return EFAULT;
        }*/
    }
    return 0;
}

void copy_in(int pid, seL4_CPtr reply_cap, copy_in_args *args, int err) {
    
    copy_in_args *copy_args = (copy_in_args *) args;
    if (SOS_DEBUG) printf("copy_in, usr_ptr: %p\n", (void *) copy_args->usr_ptr);
    
    if (err || copy_args->count == args->nbyte) {
        copy_args->cb(pid, reply_cap, copy_args->cb_args, err);
        free(args);
        return;
    } else {
        err = map_if_valid(copy_args->usr_ptr & PAGE_MASK
                          ,pid
                          ,copy_in_cb
                          ,args
                          ,reply_cap
                          );
        if (err) {
            printf("Error\n");
            copy_args->cb(pid, reply_cap, copy_args->cb_args, err);
            free(args);
        }
    }
    if (SOS_DEBUG) printf("copy_in ended\n");
}

void copy_in_cb(int pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (SOS_DEBUG) printf("copy_in_cb\n");
    
    copy_in_args *args = _args;
    
    if (err) {
        eprintf("Error caught in copy_in_cb\n");
        args->cb(pid, reply_cap, args->cb_args, err);
        free(args);
        return;   
    }

    //only copy up the end of a user page
    int to_copy = args->nbyte - args->count;
    if ((args->usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
        to_copy = PAGE_SIZE - (args->usr_ptr & ~PAGE_MASK);
    } 

    //get the actual mapping of the user page
    seL4_Word src = user_to_kernel_ptr(args->usr_ptr, pid);

    //copy in the data
    memcpy((void *) (args->k_ptr + args->count), (void *) src, to_copy);

    //increment the pointers etc. 
    args->count += to_copy;
    args->usr_ptr += to_copy;

    copy_in(pid, reply_cap, args, 0);
    if (SOS_DEBUG) printf("copy_in_cb ended\n");
}

//copy from kernel ptr to usr ptr 
void copy_out(int pid, seL4_CPtr reply_cap, copy_out_args* args, int err) {
    if (SOS_DEBUG) printf("copy_out\n");
    if (err || args->count == args->nbyte) {
        args->cb(pid, reply_cap, args, err);
    } else {
        int to_copy = args->nbyte - args->count;
        if ((args->usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
            to_copy = PAGE_SIZE - (args->usr_ptr & ~PAGE_MASK);
        } 
        err = copy_page(args->usr_ptr + args->count
                       ,to_copy
                       ,args->src + args->count
                       ,pid
                       ,copy_out_cb
                       ,args
                       ,reply_cap
                       );
        if (err) {
            args->cb(pid, reply_cap, args, err);
        }
    }
    if (SOS_DEBUG) printf("copy out ended\n");
} 

void copy_out_cb (int pid, seL4_CPtr reply_cap, void *args, int err) {
    if (SOS_DEBUG) printf("copy_out_cb\n");
    copy_out_args *copy_args = args;
    int to_copy = copy_args->nbyte - copy_args->count;
    if ((copy_args->usr_ptr & ~PAGE_MASK) + to_copy > PAGE_SIZE) {
        to_copy = PAGE_SIZE - (copy_args->usr_ptr & ~PAGE_MASK);
    }
    copy_args->count += to_copy;
    copy_out(pid, reply_cap, args, err);
    if (SOS_DEBUG) printf("copy_out_cb ended\n");
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
    if (copy_args == NULL) {
        eprintf("Error caught in copy_page\n");
        return -1;
    }

    copy_args->dst = dst;
    copy_args->src = src;
    copy_args->count = count;
    copy_args->cb = cb;
    copy_args->cb_args = cb_args;
    int err = map_if_valid(dst & PAGE_MASK, pid, copy_page_cb, copy_args, reply_cap);
    if (err) {
        eprintf("Error caught in copy_page\n");
        return -1;
    }
    return 0;
}

void copy_page_cb(int pid, seL4_CPtr reply_cap, void *args, int err) {
    if (SOS_DEBUG) printf("copy_page_cb\n");
    copy_page_args *copy_args = (copy_page_args *) args;
    seL4_Word kptr = user_to_kernel_ptr(copy_args->dst, pid);
    memcpy((void *)kptr, (void *)copy_args->src , copy_args->count);
    copy_args->cb(pid, reply_cap, copy_args->cb_args, err);

    free(args);
    if (SOS_DEBUG) printf("copy_page_cb ended\n");
}
