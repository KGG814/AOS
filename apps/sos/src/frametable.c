
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

#define verbose 5


#define CEIL_DIV(num, den) ((num - 1)/(den) + 1)



/* Frame status bits */
#define FRAME_INVALID    	(1 << 31) //not managed by our frame table 
#define FRAME_IN_USE        (1 << 30) //frame is in use and managed by us
#define FRAME_DONT_SWAP     (1 << 29) //frame is not to be swapped
//this bit is 1 if the frame should be swapped on the next pass of the clock
#define FRAME_SWAP_MARKED   (1 << 28)
#define PAGE_BITS           12
#define MAX_FRAMES          256
#define verbose 5

int ft_initialised = 0;
int frame_num = 0;
//frametable is essentially a list stack of free frames 

static seL4_Word low;
static seL4_Word high;

seL4_Word buffer_head = -1;
seL4_Word buffer_tail = -1;

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
        frametable[i].frame_status = FRAME_INVALID;
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
    /* Check frame table has been initialised */
    
    if (ft_initialised != 1) {
        return FRAMETABLE_NOT_INITIALISED; 
    }

    int err = 0;

    seL4_Word pt_addr = ut_alloc(seL4_PageBits);
    int index = 0;
    if (pt_addr < low) { //no frames available
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

    
    if (buffer_head == -1) {
        buffer_head = index;
    } else {
        frametable[buffer_tail].frame_status &= ~SWAP_BUFFER_MASK;
        frametable[buffer_tail].frame_status |= index;
    }
    frametable[index].frame_status = FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT) | buffer_head;
    buffer_tail = index;
    *vaddr = paddr_to_vaddr(pt_addr);
    if (map) {
        seL4_Word *tmp = (seL4_Word *) *vaddr;
        for (int i = 0; i < 1024; i++) {
            tmp[i] = 0;
        }
    }  
    frame_num++;
    printf("Allocated frame %d at index %d with pid %d and status %p\n", frame_num, index, pid, (void *)frametable[index].frame_status);
    return index;
}


//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is 
//retyped into a frame, and the frame is mapped into the SOS window at a fixed 
//offset of the physical address.
void frame_alloc_swap(int pid, seL4_CPtr reply_cap, void *args) {
    /* Check frame table has been initialised */;
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    if (ft_initialised != 1) {
        free(args);
        send_seL4_reply(reply_cap, FRAMETABLE_ERR);
    }

    int err = 0;

    alloc_args->pt_addr = ut_alloc(seL4_PageBits);
    alloc_args->index = -1;
    if (alloc_args->pt_addr < low || frame_num >= MAX_FRAMES) { //no frames available
        write_swap_args *write_args = malloc(sizeof(write_swap_args));
        write_args->cb = frame_alloc_cb;
        write_args->cb_args = args;
        write_args->pid = pid;
        write_args->reply_cap = reply_cap;
        write_args->index = get_next_frame_to_swap();
        // Write frame to current free swap slot
        write_to_swap_slot(pid, reply_cap, write_args);
        printf("write_to_swap_slot returned\n");
    } else {
        alloc_args->index = (alloc_args->pt_addr - low) / PAGE_SIZE;
        err |= cspace_ut_retype_addr(alloc_args->pt_addr
                                ,seL4_ARM_SmallPageObject
                                ,seL4_PageBits
                                ,cur_cspace
                                ,&frametable[alloc_args->index].frame_cap
                                ); 
        //9242_TODO: interpret this error correctly
        if (err) { 
            free(args);
            send_seL4_reply(reply_cap, FRAMETABLE_ERR);
        }
        
        frame_alloc_cb(pid, reply_cap, args);
    }
}

void frame_alloc_cb(int pid, seL4_CPtr reply_cap, void *args) {
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    int err = 0;
    if (alloc_args->map) {
        err = map_page(frametable[alloc_args->index].frame_cap
                   ,seL4_CapInitThreadPD
                   ,paddr_to_vaddr(alloc_args->pt_addr)
                   ,seL4_AllRights
                   ,seL4_ARM_Default_VMAttributes
                   );
    }
    if (err) {
        send_seL4_reply(err, reply_cap);
    }

    if (buffer_head == -1) {
        buffer_head = alloc_args->index;
    } else {
        frametable[buffer_tail].frame_status &= STATUS_MASK;
        frametable[buffer_tail].frame_status |= alloc_args->index;
    }
    frametable[alloc_args->index].frame_status = FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT) | buffer_head;
    buffer_tail = alloc_args->index;
    alloc_args->vaddr = paddr_to_vaddr(alloc_args->pt_addr);
    frametable[alloc_args->index].vaddr = alloc_args->vaddr;
    if (alloc_args->map) {
        seL4_Word *tmp = (seL4_Word *) alloc_args->vaddr;
        for (int i = 0; i < 1024; i++) {
            tmp[i] = 0;
        }
    }  
    frame_num++;
    printf("Swap: Allocated frame %d at index %d with pid %d and status %p\n", 
            frame_num, alloc_args->index, pid, (void *)frametable[alloc_args->index].frame_status);
    alloc_args->cb(pid, reply_cap, args);
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
    printf("Freed frame %d at index %d\n", frame_num, index);
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
    frametable[index].frame_status |= FRAME_INVALID;
    frametable[index].frame_cap = 0;
    frame_num--;
	return FRAMETABLE_OK;
}

int get_next_frame_to_swap(void) {
    int next_swap = buffer_head;
    printf("Swapping frame index %d\n", next_swap);
    buffer_head = frametable[next_swap].frame_status & SWAP_BUFFER_MASK;
    return next_swap;
}
