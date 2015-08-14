#include "frametable.h"

#include <sys/panic.h>
#include "ut_manager/ut.h"
#include "dma.h"
#include <stdlib.h>
#include <string.h>
#include <cspace/cspace.h>
#include <mapping.h>

seL4_Word* frametable;

seL4_Word* captable;
static seL4_Word ft_addr_start
static seL4_Word num_frames;
static seL4_CPtr* ft_caps;
static seL4_Word  next_free = 0;


int frame_init(seL4_Word low, seL4_Word high) {
    /* We put the frametable at the start of the stolen memory */
	ft_addr_start = low;
    frametable = (seL4_Word *)paddrToVaddr(ft_addr_start);

    /* Calculate how many frames the frame table tracks*/
    num_frames = ((high - low) / PAGE_SIZE) + ((high - low) % PAGE_SIZE == 0 ? 0 : 1);

    /* Calculate the size of the actual frame table */
    seL4_Word frameTableSize = ((num_frames * sizeof(seL4_Word)) / PAGE_SIZE) + ((num_frames * sizeof(seL4_Word)) % PAGE_SIZE == 0 ? 0 : 1);
    /* Initialise memory to store frame table caps */
    ft_caps = (seL4_CPtr*)malloc(sizeof(seL4_CPtr) * frameTableSize);
    memset(ft_caps, 0, sizeof(seL4_CPtr) * frameTableSize);

    seL4_Word ft_addr_current = ft_addr_start;

 	seL4_ARM_VMAttributes vm_attr = 0;
    /* Loop variables */
 	int err = 0;
    seL4_Word pt_addr;
    /* Initialise the memory for the frame table */
    for (int currPage = 0; currPage < frameTableSize; currPage++) {
        /* Get the caps for the frametable */
        if(*ft_caps == seL4_CapNull){
            /* Get some memory from ut manager */
            pt_addr = ut_alloc(seL4_PageBits);
            /* Translate memory */
            //ut_translate(pt_addr, seL4_Untyped* ret_cptr, seL4_Word* ret_offset);
            /* Create the frame cap */	
            err = cspace_ut_retype_addr(pt_addr, seL4_ARM_SmallPageObject,
                                        seL4_PageBits, cur_cspace, ft_caps);

            conditional_panic(err, "Cannot create frame table cap\n");
            //conditional_panic(1, "Some error\n");
            /* Map in the frame */
            err = map_page(*ft_caps, seL4_CapInitThreadPD, paddrToVaddr(ft_addr_current), 
                           seL4_AllRights, vm_attr);
            conditional_panic(err, "Cannot map frame table page\n");
        }    
        
        ft_addr_current += (1 << seL4_PageBits);
        ft_caps++;
    }

    /*for (int ftIndex = 0; ftIndex < numFrames; ftIndex++) {
        frametable[ftIndex] = FREE;
        frametable[ftIndex] |= (ftIndex + 1) << FTE_STATE_BITS;
        
    }*/

    
    //*(int*)paddrToVaddr(ft_addr_start) = 0;
    return 0;
}
//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is retyped into a frame, 
//and the frame is mapped into the SOS window at a fixed offset of the physical address.
int frame_alloc() {
    if (next_free == INVALID) {
        seL4_Word pt_addr = ut_alloc(seL4_PageBits);
        seL4_Word index = (pt_addr - ft_addr_start) / PAGE_SIZE;
        frametable[index] |= FRAME_UT_ALLOCED;
        frametable[index] |= FRAME_ALLOCED;
        /* TODO Type and map this memory */

    } else {
       seL4_Word index = next_free;
       next_free = frametable[next_free] & FRAME_INDEX_MASK;
       frametable[next_free] |= FRAME_ALLOCED; 
    }
    return paddrToVaddr(index*PAGE_SIZE + ft_addr_start);
    /* Check frame table has been initialised */

}
//frame_free: the physical memory is no longer mapped in the window, the frame object is destroyed, and the physical memory range is returned via ut_free.
int frame_free() {
    seL4_Word index = (vaddrToPaddr(vaddr) - ft_addr_start) / PAGE_SIZE;
	return 0;
}