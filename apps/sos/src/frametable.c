
#include <sys/debug.h>
#include "frametable.h"

#include <sys/panic.h>
#include "ut_manager/ut.h"
#include "vmem_layout.h"

#include <stdlib.h>
#include <string.h>
#include <cspace/cspace.h>
#include <mapping.h>



#define CEIL_DIV(num, den) ((num)/(den) + ((num) % (den) == 0 ? 0 : 1))

/* Mask to obtain frame index */
#define FRAME_INDEX_MASK	0x000FFFFF

/* Frame status bits */
#define FRAME_INVALID    	(1 << 31) //not managed by our frame table 
#define FRAME_IN_USE        (1 << 30) //frame is in use and managed by us
#define FRAME_DONT_SWAP     (1 << 29) //frame is not to be swapped

//this bit is 1 if the frame should be swapped on the next pass of the clock
#define FRAME_SWAP_MARKED   (1 << 28)
#define verbose 5
#define PAGE_BITS           12
int ft_initialised = 0;

//frametable is essentially a list stack of free frames 

static seL4_Word low;
static seL4_Word high;

typedef struct _ft_entry {
    seL4_Word frame_status;
    seL4_CPtr frame_cap;
} ft_entry;

//sos vspace addr of ft
static ft_entry* frametable = (ft_entry *) FT_START_ADDR; 

static seL4_Word paddrToVaddr(seL4_Word paddr) { 
    return paddr + VM_START_ADDR;
}
static seL4_Word vaddrToPaddr(seL4_Word vaddr) { 
    return vaddr - VM_START_ADDR;
}

int frame_init(void) {
    if (ft_initialised == 1) {
        return FT_INITIALISED;
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
        real_indices[i].frame_status = (pt_addr - low) / PAGE_SIZE;
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

    return FT_OK;
    //return paddrToVaddr(pt_addr);
}
//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is 
//retyped into a frame, and the frame is mapped into the SOS window at a fixed 
//offset of the physical address.
seL4_Word frame_alloc(void) {

    /* Check frame table has been initialised */
    
    if (ft_initialised != 1) {
        //this is not the correct behaviour; we should instead steal_mem or something 
        return 0; 
    }

    int err = 0;
    seL4_ARM_VMAttributes vm_attr = 0;

    seL4_Word pt_addr = ut_alloc(seL4_PageBits);
    if (pt_addr < low) { //no frames available
        // Some stuff
        return 0;
    }
    seL4_Word index = (pt_addr - low) / PAGE_SIZE;
    err |= cspace_ut_retype_addr(pt_addr
                                ,seL4_ARM_SmallPageObject
                                ,seL4_PageBits
                                ,cur_cspace
                                ,&frametable[index].frame_cap
                                );
    err |= map_page(frametable[index].frame_cap
                   ,seL4_CapInitThreadPD
                   ,paddrToVaddr(pt_addr)
                   ,seL4_AllRights
                   ,vm_attr
                   );
    if (err) {
        return 0;
    }
 
    //set the status bits of the new frame 
    frametable[index].frame_status = FRAME_IN_USE;

    return paddrToVaddr(pt_addr);
    //return FT_OK;
}
//frame_free: the physical memory is no longer mapped in the window, the frame 
//object is destroyed, and the physical memory range is returned via ut_free.
int frame_free(seL4_Word vaddr) {
    if (ft_initialised != 1) {
        //this is not the correct behaviour; we should instead steal_mem or something 
        return FT_NOT_INITIALISED; 
    }
    seL4_Word paddr = vaddrToPaddr(vaddr);
    //TODO: check we have a valid vaddr 
    if ((paddr < low) || (paddr > high)) {
        return FT_ERR;
    }

    seL4_Word index = (paddr - low) / PAGE_SIZE;
    //tried to free a free frame 
    if (!(frametable[index].frame_status & FRAME_IN_USE)) {
        return FT_ERR;
    } 
        
    //do any sort of untyping/retyping capping/uncapping here 
    seL4_ARM_Page_Unmap(frametable[index].frame_cap);
    
    int err = cspace_revoke_cap(cur_cspace, frametable[index].frame_cap);

    if (err) {
        return FT_ERR;
    }

    err = cspace_delete_cap(cur_cspace, frametable[index].frame_cap); 
    if (err) {
        return FT_ERR;
    }
    ut_free(vaddrToPaddr(vaddr), PAGE_BITS);

    //set status bits here.
    frametable[index].frame_status = FRAME_INVALID;
    frametable[index].frame_cap = 0;

	return FT_OK;
}
