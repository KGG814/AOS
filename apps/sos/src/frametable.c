
#include <sys/debug.h>
#include "frametable.h"

#include <sys/panic.h>
#include "ut_manager/ut.h"
#include <sos/vmem_layout.h>
#include <stdlib.h>
#include <string.h>
#include <cspace/cspace.h>
#include <mapping.h>
#include <sys/debug.h>
#include "swap.h"
#include "syscalls.h"
#include "pagetable.h"
#include "debug.h"

#define verbose 5


#define CEIL_DIV(num, den) ((num - 1)/(den) + 1)





#define FRAME_STATUS_MASK   (0xF0000000)
#define PAGE_BITS           12
#define MAX_FRAMES          120
#define verbose 5
#define FT_INITIALISED      1
int ft_initialised = 0;
int frame_num = 0;
//frametable is essentially a list stack of free frames 

static seL4_Word low;
static seL4_Word high;

extern int buffer_head;
extern int buffer_tail;

void frame_alloc_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args, int err);
int get_next_frame_to_swap(void);

// Convert from physical address to kernel virtual address
static seL4_Word paddr_to_vaddr(seL4_Word paddr) { 
    return paddr + VM_START_ADDR;
}

// Convert from frametable index to physical address
seL4_Word index_to_paddr(int index) {
    return index * PAGE_SIZE + low;
}

int paddr_to_index(seL4_Word paddr) {
    return (paddr - low) / PAGE_SIZE;
}

// Convert from frametable index to kernel virtual address
seL4_Word index_to_vaddr(int index) {
    return (index * PAGE_SIZE + low) + VM_START_ADDR;
}

// Convert from kernel virtual address to frametable index
int vaddr_to_index(seL4_Word vaddr) {
    return (vaddr - VM_START_ADDR - low)/PAGE_SIZE;
}

// Initialise frame table
int frame_init(void) {
    frametable = (ft_entry *) FT_START_ADDR;
    if (ft_initialised) {
        return FRAMETABLE_INITIALISED;
    }
    seL4_Word num_frames;
    ut_find_memory(&low, &high);

    /* Calculate how many frames the frame table tracks*/
    num_frames = CEIL_DIV((high - low), PAGE_SIZE);

    /* Calculate the size of the actual frame table */
    seL4_Word frame_table_size = CEIL_DIV((num_frames * sizeof(ft_entry)), PAGE_SIZE);

    //temporary array to store the real indices and caps of the frames used for 
    //the frame table 
    ft_entry *real_indices = malloc(sizeof(ft_entry) * frame_table_size);

    //dummy var to get caps
    seL4_CPtr curr_cap = 0;

 	seL4_ARM_VMAttributes vm_attr = 0;
    /* Loop variables */
 	int err = 0;
    seL4_Word pt_addr;
    /* Initialise the memory for the frame table */
    for (int i = 0; i < frame_table_size; i++) {
        
        /* Get some memory from ut manager */
        pt_addr = ut_alloc(seL4_PageBits);
        conditional_panic(pt_addr == 0, "Cannot get memory for frame table\n");

        /* Create the frame cap */	
        err = cspace_ut_retype_addr(pt_addr, seL4_ARM_SmallPageObject,
                                    seL4_PageBits, cur_cspace, &curr_cap);
        conditional_panic(err, "Cannot create frame table cap\n");

        /* Map in the frame, once into the ft, once into the sos vspace */
        err = map_page(curr_cap
                      ,seL4_CapInitThreadPD
                      ,((seL4_Word) frametable) + (i * PAGE_SIZE)
                      ,seL4_AllRights, vm_attr
                      );
        conditional_panic(err, "Cannot map frame table page\n");
        
        //store the real frame number 
        real_indices[i].frame_status = ((pt_addr - low) / PAGE_SIZE) & SWAP_BUFFER_MASK;
        real_indices[i].frame_cap = curr_cap;
        curr_cap = 0;
    }

    //currently, the only frames we have from ut are the frames for the frame 
    //table. we need to say the rest of the frames are invalid.
    for (int i = 0; i < num_frames; i++) {
        frametable[i].frame_status = 0;
        frametable[i].frame_cap = 0;
        frametable[i].mapping_cap = 0;
        frametable[i].vaddr = 0;
    }

    int index = 0;
    for (int i = 0; i < frame_table_size; i++) {
        index = real_indices[i].frame_status;
        printf("Setting index %p to don't swap\n", (void *) index);
        frametable[index].frame_status = FRAME_IN_USE | FRAME_DONT_SWAP;
        frametable[index].frame_cap = real_indices[i].frame_cap;
    }

    free(real_indices);

    ft_initialised = FT_INITIALISED;

    return FRAMETABLE_OK;
}

// Allocate a frame and map it into kernel virtual memory, if the field is set
void frame_alloc_swap(int pid, seL4_CPtr reply_cap, frame_alloc_args *args, int err) {
    if (SOS_DEBUG) printf("frame_alloc_swap\n");
    /* Check frame table has been initialised */;
    if (!ft_initialised || err) {
        // Frame table has not been initialised
        args->index = FRAMETABLE_ERR;
        args->cb(pid, reply_cap, args, -1);
        return;
    }

    // Get some memory from ut_alloc
    args->pt_addr = ut_alloc(seL4_PageBits);
    // Check if we need to swap
    printf("frame_num %d\n", frame_num);
    if (args->pt_addr < low || frame_num >= MAX_FRAMES) {
        
        // No frames available, need to swap
        // This may not be valid if memory runs out, but is fine for artificially limiting physical memory
        ut_free(args->pt_addr, PAGE_BITS);
        // Have to write to swap file
        // Set up writE_to_swap_slot args
        write_swap_args *write_args = malloc(sizeof(write_swap_args));
        if (write_args == NULL) {
            args->index = FRAMETABLE_ERR;
            //don't free the args here, the caller of frame alloc should do that 
            args->cb(pid, reply_cap, args, -1);
            return;
        }

        write_args->cb = (callback_ptr) frame_alloc_cb;
        write_args->cb_args = args;
        write_args->pid = pid;
        write_args->reply_cap = reply_cap;

        // Get frametable index of a frame to be swapped
        write_args->index = get_next_frame_to_swap();

        // Put the index in the frame_alloc args so that we can continue after callback
        args->index = write_args->index;
        args->vaddr = index_to_vaddr(args->index);
        args->pt_addr = index_to_paddr(args->index);

        // Write frame to current free swap slot
        write_to_swap_slot(pid, reply_cap, write_args);
    } else {
        // Don't need to swap can retype memory
        // Calculate the index
        args->index = paddr_to_index(args->pt_addr);
        // Retype the cap so we can use it
        // Also put the cap in the frameable
        err |= cspace_ut_retype_addr(args->pt_addr
                                ,seL4_ARM_SmallPageObject
                                ,seL4_PageBits
                                ,cur_cspace
                                ,&frametable[args->index].frame_cap
                                ); 
        // Check if the retype succeeded
        if (err) { 
            // Retype failed, free memory and callback
            //9242_TODO More cleanup needed?
            assert(RTN_ON_FAIL);

            //9242_TODO confirm this is the right thing to do
            ut_free(args->pt_addr, PAGE_BITS);

            args->index = FRAMETABLE_ERR;
            args->cb(pid, reply_cap, args, -1);
            return;
        }
        frame_num++;
        // Continue with frame_alloc call
        frame_alloc_cb(pid, reply_cap, args, 0);
    }
    if (SOS_DEBUG) printf("frame_alloc_swap ended\n");
}

// Callback for frame_alloc
// Memory has been ut_alloced and retyped, do frametable metadata update
// and map into kernel virtual memory
void frame_alloc_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args, int err) {
    if (err) {
        args->index = FRAMETABLE_ERR;
        args->cb(pid, reply_cap, args, err);
        return;
    }


    // Get arguments we need
    int map             = args->map;
    seL4_Word pt_addr   = args->pt_addr;
    int index           = args->index;
    if (SOS_DEBUG) printf("frame_alloc_cb index %p\n", (void *) index);
    // If it needs to be mapped into kernel memory, do so
    if (map == KMAP) {
        // Calculate the virtual memory for this physical address
        printf("Kernel map\n");
        seL4_Word vaddr = paddr_to_vaddr(pt_addr);
        // Map it in, using the cap in the frametable
        int err = map_page(frametable[index].frame_cap
                          ,seL4_CapInitThreadPD
                          ,vaddr
                          ,seL4_AllRights
                          ,seL4_ARM_Default_VMAttributes
                          );

        // If there was an error, reply to the callback
        if (err) {
            //9242_TODO is this the right thing to do?
            frame_free(index);

            args->index = 0;
            args->cb(pid, reply_cap, args, err);
            return;
        }

        printf("touching page %d\n", *((int *) vaddr));
        // If we are mapping it into kernel, clear the memory
        memset((void *) vaddr, 0, PAGE_SIZE);
    }
    // Check if the frame is in the swap buffer
    if (!(frametable[index].frame_status & SWAP_BUFFER_MASK)) {
        
        // Not in the swap buffer, need to put it in
        // Check if swap buffer has been initialised
        if (buffer_head == -1) {
            // Not initialised, set head to this frame
            buffer_head = index;
        } else {
            // Has been initialised, make the tail point to this frame
            // Clear the buffer bits
            frametable[buffer_tail].frame_status &= ~SWAP_BUFFER_MASK;
            // Set the buffer bits
            assert(index != 0);
            frametable[buffer_tail].frame_status |= index;
        }
        if (SOS_DEBUG) printf("Not in swap buffer %d, %d\n", index, buffer_head);
        // Set the tail to the new buffer
        buffer_tail = index;
        // Make the new frame (which is now the tail), point to the head
        frametable[index].frame_status &= ~PROCESS_MASK;
        frametable[index].frame_status &= ~SWAP_BUFFER_MASK;
        frametable[index].frame_status |= FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT) | buffer_head;
        if (SOS_DEBUG) printf("%d\n",frametable[index].frame_status & SWAP_BUFFER_MASK);
    } else {
        // Already in the swap buffer, just set the pid and status bits
        // This saves us having to remove it from the list and put it at the end, 
        // which would require a doubly linked buffer
        // Clear the pid
        frametable[index].frame_status &= ~PROCESS_MASK;
        // Set the pid and set it to a vlid frame
        frametable[index].frame_status |= FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT);
    }
    if (SOS_DEBUG) printf("%d\n",frametable[index].frame_status & SWAP_BUFFER_MASK);
    assert((frametable[index].frame_status & SWAP_BUFFER_MASK) != 0);
    // Set the vaddr in the return values
    args->vaddr = paddr_to_vaddr(pt_addr);
    
    // Debug print
    if (SOS_DEBUG) {
        printf("Allocated frame %p at index %p with pid %d and status %p with head %p, tail %p, vaddr %p\n", 
            (void *) frame_num,(void *)  args->index, pid, 
            (void *)frametable[args->index].frame_status, (void *) buffer_head, (void *) buffer_tail,
            (void *) args->vaddr);
    } 
    // Do callback
    // Increment number of allocated frames
    
    // Increment counter for process
    proc_table[pid]->size++;
    args->cb(pid, reply_cap, args, 0);
    if (SOS_DEBUG) printf("frame_alloc_cb ended, index %p\n", (void *) args->index);
}

//frame_free: the physical memory is no longer mapped in the window, the frame 
//object is destroyed, and the physical memory range is returned via ut_free.
int frame_free(int index) {
    if (ft_initialised != 1) {
        return FRAMETABLE_NOT_INITIALISED; 
    }

    //tried to free a free frame 
    if (!(frametable[index].frame_status & FRAME_IN_USE)) {
        return FRAMETABLE_ERR;
    } 
        
    //do any sort of untyping/retyping capping/uncapping here 
    seL4_ARM_Page_Unmap(frametable[index].frame_cap);
    if (SOS_DEBUG) printf("Freed frame %d at index %d\n", frame_num, index);
    int err = cspace_revoke_cap(cur_cspace, frametable[index].frame_cap);

    if (err) {
        return FRAMETABLE_ERR;
    }

    err = cspace_delete_cap(cur_cspace, frametable[index].frame_cap); 
    if (err) {
        return FRAMETABLE_ERR;
    }
    seL4_Word pt_addr = index_to_paddr(index);
    ut_free(pt_addr, PAGE_BITS);

    //set status bits here.
    frametable[index].frame_status &= ~STATUS_MASK;
    frametable[index].frame_cap = 0;
    frametable[index].mapping_cap = 0;
    frametable[index].vaddr = 0;
    frame_num--;
    if (frame_num < 0) {
        frame_num = 0;
    }
	return FRAMETABLE_OK;
}

int get_next_frame_to_swap(void) {
    if (SOS_DEBUG) printf("get_next_frame_to_swap\n");
    int curr_frame = buffer_tail;
    int next_frame = frametable[curr_frame].frame_status & SWAP_BUFFER_MASK;
    assert((next_frame & SWAP_BUFFER_MASK) != 0);
    while (1) {
        if (SOS_DEBUG) printf("curr_frame %p next_frame: %p\n", (void *) curr_frame, (void *) next_frame);
        if (next_frame == 0) {
            //9242_TODO Fix this sporadic bug
            curr_frame = buffer_tail;
            next_frame = frametable[curr_frame].frame_status & SWAP_BUFFER_MASK;
            break;
        }
        
        int status = frametable[next_frame].frame_status;
        int swap_pid = (status & PROCESS_MASK) >> PROCESS_BIT_SHIFT;
        if (!(status & FRAME_IN_USE) 
        || (proc_table[swap_pid] == NULL) 
        || (proc_table[swap_pid]->status != PROC_READY)
        ) {
            //9242_TODO Remove from buffer
            break;
        }
        if (status & FRAME_DONT_SWAP) {
        } else {
            if (status & FRAME_SWAP_MARKED) {     
                frametable[next_frame].frame_status &= ~FRAME_SWAP_MARKED;       
                break;
            } else if (status & FRAME_IN_USE) {
                frametable[next_frame].frame_status |= FRAME_SWAP_MARKED;
                /* Unmap and map back into kernel only */
                seL4_ARM_Page_Unify_Instruction(frametable[next_frame].mapping_cap, 0, PAGESIZE);
                seL4_ARM_Page_Unmap(frametable[next_frame].frame_cap);
                cspace_revoke_cap(cur_cspace, frametable[next_frame].frame_cap);
                int err = map_page(frametable[next_frame].frame_cap
                                  ,seL4_CapInitThreadPD
                                  ,index_to_vaddr(next_frame)
                                  ,seL4_AllRights
                                  ,seL4_ARM_Default_VMAttributes
                //this should always work
                assert(err == 0);
                frametable[next_frame].mapping_cap = 0;
            }
        }

        curr_frame = next_frame;
        next_frame = status & SWAP_BUFFER_MASK;
    }
    if (SOS_DEBUG) printf("Swapping frame index %p\n", (void *) next_frame);

    /*assert(frametable[next_frame].frame_status & SWAP_BUFFER_MASK != 0);
    buffer_tail = curr_frame;
    buffer_head = frametable[next_frame].frame_status & SWAP_BUFFER_MASK;
    frametable[curr_frame].frame_status &= ~SWAP_BUFFER_MASK;
    frametable[curr_frame].frame_status |= buffer_head;
    assert(buffer_head & SWAP_BUFFER_MASK != 0);
    frametable[next_frame].frame_status &= ~FRAME_STATUS_MASK;
    frametable[next_frame].frame_status |= FRAME_DONT_SWAP;
    assert(frametable[next_frame].frame_status & SWAP_BUFFER_MASK != 0);*/
    buffer_tail = frametable[next_frame].frame_status & SWAP_BUFFER_MASK;
    buffer_head = frametable[buffer_tail].frame_status & SWAP_BUFFER_MASK;
    if (SOS_DEBUG) printf("get_next_frame_to_swap ended\n");
    return next_frame;
}
