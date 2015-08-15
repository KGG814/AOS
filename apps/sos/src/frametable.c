#include "frametable.h"

#include <sys/panic.h>
#include "ut_manager/ut.h"
#include "dma.h"
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

int ft_initialised = 0;

//frametable is essentially a list stack of free frames 
seL4_Word* frametable;
static seL4_CPtr* ft_caps; //caps for the frames the frametable sits in
static seL4_Word num_frames; //number of frames we manage

static seL4_Word ft_addr_start; //this is also the offset for frame index -> paddr
//initialise this to an invalid frame
static seL4_Word next_free = FRAME_INVALID;

int frame_init(seL4_Word low, seL4_Word high) {
    if (ft_initialised == 1) {
        return FT_INITIALISED;
    }
	ft_addr_start = low;
    frametable = (seL4_Word *)paddrToVaddr(ft_addr_start);

    /* Calculate how many frames the frame table tracks*/
    //num_frames = ((high - low) / PAGE_SIZE) + ((high - low) % PAGE_SIZE == 0 ? 0 : 1);
    num_frames = CEIL_DIV((high - low), PAGE_SIZE);

    /* Calculate the size of the actual frame table */
    //seL4_Word frameTableSize = ((num_frames * sizeof(seL4_Word)) / PAGE_SIZE) 
    //+ ((num_frames * sizeof(seL4_Word)) % PAGE_SIZE == 0 ? 0 : 1);
    seL4_Word frame_table_size = CEIL_DIV((num_frames * sizeof(seL4_Word)), PAGE_SIZE);
    /* Initialise memory to store frame table caps */
    ft_caps = (seL4_CPtr*)malloc(sizeof(seL4_CPtr) * frame_table_size);
    memset(ft_caps, 0, sizeof(seL4_CPtr) * frame_table_size);

    seL4_Word ft_addr_current = ft_addr_start;

 	seL4_ARM_VMAttributes vm_attr = 0;
    /* Loop variables */
 	int err = 0;
    seL4_Word pt_addr;

    /* Initialise the memory for the frame table */
    for (int i = 0; i < frame_table_size; i++) {
        /* Get the caps for the frametable */
        if(ft_caps[i] == seL4_CapNull){
            /* Get some memory from ut manager */
            pt_addr = ut_alloc(seL4_PageBits);
            
            /* Translate memory */
            //ut_translate(pt_addr, seL4_Untyped* ret_cptr, seL4_Word* ret_offset);

            /* Create the frame cap */	
            err = cspace_ut_retype_addr(pt_addr, seL4_ARM_SmallPageObject,
                                        seL4_PageBits, cur_cspace, &ft_caps[i]);

            conditional_panic(err, "Cannot create frame table cap\n");

            /* Map in the frame */
            err = map_page(ft_caps[i], seL4_CapInitThreadPD, paddrToVaddr(ft_addr_current), 
                           seL4_AllRights, vm_attr);
            conditional_panic(err, "Cannot map frame table page\n");
        }    
        
        //set the corresponding page in the frame table to 1. in use, 2. not to be swapped
        //note that the page that contains this entry will have been alloced by
        //now, so it's safe to access.
        frametable[i] = FRAME_IN_USE | FRAME_DONT_SWAP;
        //increment the pointer to the next page
        ft_addr_current += (1 << seL4_PageBits);
    }

    //currently, the only frames we have from ut are the frames for the frame 
    //table. we need to say the rest of the frames are invalid.
    for (int i = frame_table_size; i < num_frames; i++) {
        frametable[i] = FRAME_INVALID;
    }
    
    ft_initialised = 1;

    //*(int*)paddrToVaddr(ft_addr_start) = 0;
    return FT_OK;
}
//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is retyped into a frame, 
//and the frame is mapped into the SOS window at a fixed offset of the physical address.
int frame_alloc(seL4_Word *dst) {
    /* Check frame table has been initialised */
    if (ft_initialised != 1) {
        //this is not the correct behaviour; we should instead steal_mem or something 
        return FT_NOT_INITILIASED; 
    }

    seL4_Word index = next_free;
    if (index & FRAME_INVALID) { //no frames available, need to call ut_alloc
        seL4_Word pt_addr = ut_alloc(seL4_PageBits);
        index = (pt_addr - ft_addr_start) / PAGE_SIZE;
        //set the status bits of the new frame 
        frametable[index] = FRAME_IN_USE;
    } else { //there's a frame available, get it from the stack
        //update the top of the stack. this is either an invalid frame or a free 
        //frame 
        next_free = frametable[next_free];

        //set teh status bits of the new frame
        frametable[index] = FRAME_IN_USE;
    }

    /* TODO Type and map this memory */

    *dst = paddrToVaddr(index*PAGE_SIZE + ft_addr_start);
    return FT_OK;
}
//frame_free: the physical memory is no longer mapped in the window, the frame 
//object is destroyed, and the physical memory range is returned via ut_free.
int frame_free(seL4_Word vaddr) {
    //TODO: check we have a valid vaddr 

    seL4_Word index = (vaddrToPaddr(vaddr) - ft_addr_start) / PAGE_SIZE;
    //tried to free a free frame 
    if (!(frametable[index] & FRAME_IN_USE)) {
        return FT_ERR;
    } 
        
    //do any sort of untyping/retyping capping/uncapping here 

    //set status bits here.
    frametable[index] = next_free;
    next_free = index; 

	return FT_OK;
}
