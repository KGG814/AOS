
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
#define MAX_FRAMES          80
#define verbose 5

int ft_initialised = 0;
int frame_num = 0;
//frametable is essentially a list stack of free frames 

static seL4_Word low;
static seL4_Word high;

extern int buffer_head;
extern int buffer_tail;

void frame_alloc_cb(int pid, seL4_CPtr reply_cap, void *args);
int get_next_frame_to_swap(void);

static seL4_Word paddr_to_vaddr(seL4_Word paddr) { 
    return paddr + VM_START_ADDR;
}

seL4_Word index_to_paddr(int index) {
    return index * PAGE_SIZE + low;
}

seL4_Word index_to_vaddr(int index) {
    return (index * PAGE_SIZE + low) + VM_START_ADDR;
}

int vaddr_to_index(seL4_Word vaddr) {
    return (vaddr - VM_START_ADDR - low)/PAGE_SIZE;
}

int frame_init(void) {
    frametable = (ft_entry *) FT_START_ADDR;
    if (ft_initialised == 1) {
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
    }

    int index = 0;
    for (int i = 0; i < frame_table_size; i++) {
        index = real_indices[i].frame_status;
        frametable[index].frame_status = FRAME_IN_USE | FRAME_DONT_SWAP;
        frametable[index].frame_cap = real_indices[i].frame_cap;
    }

    free(real_indices);

    ft_initialised = 1;

    return FRAMETABLE_OK;
}

int frame_alloc(seL4_Word *vaddr, int map, int pid) {
    if (SOS_DEBUG) printf("frame_alloc\n");
    /* Check frame table has been initialised */
    
    if (ft_initialised != 1) {
        return FRAMETABLE_NOT_INITIALISED; 
    }

    int err = 0;

    seL4_Word pt_addr = ut_alloc(seL4_PageBits);
    int index = 0;
    if (pt_addr < low || frame_num >= MAX_FRAMES) { //no frames available
        assert(1);     
    } else {
        index = (pt_addr - low) / PAGE_SIZE;
        err |= cspace_ut_retype_addr(pt_addr
                                ,seL4_ARM_SmallPageObject
                                ,seL4_PageBits
                                ,cur_cspace
                                ,&frametable[index].frame_cap
                                ); 
    }
    //9242_TODO: interpret this error correctly
    if (err) { 
        ut_free(pt_addr, PAGE_BITS);
        return FRAMETABLE_ERR;
    }

    if (map) {
        err |= map_page(frametable[index].frame_cap
                   ,seL4_CapInitThreadPD
                   ,paddr_to_vaddr(pt_addr)
                   ,seL4_AllRights
                   ,seL4_ARM_Default_VMAttributes
                   );
    }
    
    //9242_TODO: interpret this error correctly.
    if (err) { 
        return FRAMETABLE_ERR;
    }
    //set the status bits of the new frame 

    *vaddr = paddr_to_vaddr(pt_addr);
    if (map) {
        seL4_Word *tmp = (seL4_Word *) *vaddr;
        for (int i = 0; i < 1024; i++) {
            tmp[i] = 0;
        }
    }  

    int status = FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT);
    // Not in the swap buffer, need to put it in
    /* Swap buffer */
    if (buffer_head == -1) {
        buffer_head = index;
    } else {
        frametable[buffer_tail].frame_status &= ~SWAP_BUFFER_MASK;
        frametable[buffer_tail].frame_status |= index;
    }
    status |= buffer_head;
    buffer_tail = index;
    frametable[index].frame_status = status;

    frame_num++;
    printf("Allocated frame %d at index %d with pid %d and status %p\n"
          ,frame_num
          ,index
          ,pid
          ,(void *)frametable[index].frame_status
          );
    return index;
}


//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is 
//retyped into a frame, and the frame is mapped into the SOS window at a fixed 
//offset of the physical address.
void frame_alloc_swap(int pid, seL4_CPtr reply_cap, void *_args) {
    if (SOS_DEBUG) printf("frame_alloc_swap\n");
    /* Check frame table has been initialised */;
    frame_alloc_args *args = (frame_alloc_args *) _args;
    args->index = FRAMETABLE_ERR;

    if (ft_initialised != 1) {
        args->cb(pid, reply_cap, args);
    }

    int err = 0;

    args->pt_addr = ut_alloc(seL4_PageBits);
    if (args->pt_addr < low || frame_num >= MAX_FRAMES) { //no frames available
        write_swap_args *write_args = malloc(sizeof(write_swap_args));
        if (write_args == NULL) {
            args->cb(pid, reply_cap, args);
        }

        write_args->cb = frame_alloc_cb;
        write_args->cb_args = args;
        write_args->pid = pid;
        write_args->reply_cap = reply_cap;
        write_args->index = get_next_frame_to_swap();

        args->index = write_args->index;
        // Write frame to current free swap slot
        write_to_swap_slot(pid, reply_cap, write_args);
    } else {
        args->index = (args->pt_addr - low) / PAGE_SIZE;
        err |= cspace_ut_retype_addr(args->pt_addr
                                ,seL4_ARM_SmallPageObject
                                ,seL4_PageBits
                                ,cur_cspace
                                ,&frametable[args->index].frame_cap
                                ); 
        //9242_TODO: interpret this error correctly
        if (err) { 
            ut_free(args->pt_addr, PAGE_BITS);
            args->index = FRAMETABLE_ERR;
            args->cb(pid, reply_cap, args);
        }
        
        frame_alloc_cb(pid, reply_cap, args);
    }
    if (SOS_DEBUG) printf("frame_alloc_swap ended\n");
}

void frame_alloc_cb(int pid, seL4_CPtr reply_cap, void *args) {
    if (SOS_DEBUG) printf("frame_alloc_cb\n");
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    int err = 0;
    if (alloc_args->map) {
        seL4_Word vaddr = paddr_to_vaddr(alloc_args->pt_addr);
        err = map_page(frametable[alloc_args->index].frame_cap
                      ,seL4_CapInitThreadPD
                      ,vaddr
                      ,seL4_AllRights
                      ,seL4_ARM_Default_VMAttributes
                      );
    }
    if (err) {
        send_seL4_reply(err, reply_cap);
    }
    int status = FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT);
    if (!(frametable[alloc_args->index].frame_status & SWAP_BUFFER_MASK)) {
        // Not in the swap buffer, need to put it in
        /* Swap buffer */
        //printf("Not in swap buffer\n");
        if (buffer_head == -1) {
            buffer_head = alloc_args->index;
        } else {
            frametable[buffer_tail].frame_status &= ~SWAP_BUFFER_MASK;
            frametable[buffer_tail].frame_status |= alloc_args->index;
        }
        status |= buffer_head;
        buffer_tail = alloc_args->index;
        frametable[alloc_args->index].frame_status = status;
    } else {
        frametable[buffer_tail].frame_status &= ~PROCESS_MASK;
        frametable[buffer_tail].frame_status |= FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT);
    }

    alloc_args->vaddr = paddr_to_vaddr(alloc_args->pt_addr);
    if (alloc_args->map) {
        seL4_Word *tmp = (seL4_Word *) alloc_args->vaddr;
        for (int i = 0; i < 1024; i++) {
            tmp[i] = 0;
        }
    }  
    frame_num++;
    //printf("Swap: Allocated frame %p at index %p with pid %d and status %p with head %p, tail %p\n", 
            //(void *) frame_num,(void *)  alloc_args->index, pid, 
            //(void *)frametable[alloc_args->index].frame_status, (void *) buffer_head, (void *) buffer_tail);
    alloc_args->cb(pid, reply_cap, args);
    if (SOS_DEBUG) printf("frame_alloc_cb ended\n");
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
    frame_num--;
	return FRAMETABLE_OK;
}

int get_next_frame_to_swap(void) {
    if (SOS_DEBUG) printf("get_next_frame_to_swap\n");
    int curr_frame = buffer_tail;
    int next_frame = frametable[curr_frame].frame_status & SWAP_BUFFER_MASK;
    while (1) {
        //printf("curr_frame %p next_frame: %p\n", (void *) curr_frame, (void *) next_frame);
        if (next_frame == 0) {
          assert(1==0);  
        }
        
        int status = frametable[next_frame].frame_status;
        if (status & FRAME_DONT_SWAP) {
        } else {
            if (status & FRAME_SWAP_MARKED) {            
                break;
            } else if (status & FRAME_IN_USE) {
                frametable[next_frame].frame_status |= FRAME_SWAP_MARKED;
                /* Unmap and map back into kernel only */
                cspace_revoke_cap(cur_cspace, frametable[next_frame].frame_cap);
                seL4_ARM_Page_Unmap(frametable[next_frame].frame_cap);
                int err = map_page(frametable[next_frame].frame_cap
                   ,seL4_CapInitThreadPD
                   ,index_to_vaddr(next_frame)
                   ,seL4_AllRights
                   ,seL4_ARM_Default_VMAttributes
                   );
                assert(err == 0);
            }
        }

        curr_frame = next_frame;
        next_frame = status & SWAP_BUFFER_MASK;
    }
    if (SOS_DEBUG) printf("Swapping frame index %p\n", (void *) next_frame);

    buffer_tail = curr_frame;
    buffer_head = frametable[next_frame].frame_status & SWAP_BUFFER_MASK;
    frametable[curr_frame].frame_status &= ~SWAP_BUFFER_MASK;
    frametable[curr_frame].frame_status |= frametable[next_frame].frame_status & SWAP_BUFFER_MASK;
    frametable[next_frame].frame_status &= ~FRAME_STATUS_MASK;
    frametable[next_frame].frame_status |= FRAME_DONT_SWAP;
    if (SOS_DEBUG) printf("get_next_frame_to_swap ended\n");
    return next_frame;
}
