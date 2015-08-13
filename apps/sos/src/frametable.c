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
static seL4_CPtr* ft_caps;
static seL4_CPtr* ct_caps;


int frame_init(seL4_Word addr) {
    /* We put the frametable at the start of the stolen memory */
	seL4_Word ft_addr_start = addr;
    frametable = (seL4_Word *)ft_addr_start
    seL4_Word ft_addr_end = ft_addr_start + (1 << FT_SIZE_BITS);
    /* Initialise memory to store frame table caps */
	ft_caps = (seL4_CPtr*)malloc(sizeof(seL4_CPtr) * FT_PAGES);
	memset(ft_caps, 0, sizeof(seL4_CPtr) * FT_PAGES);

    /* Cap table goes straight after frame table */
    seL4_Word ct_addr_start = ft_addr_end;
    captable = (seL4_Word *)cp_addr_end;
    seL4_Word ct_addr_end = ft_addr_start + (1 << CT_SIZE_BITS);
    /*  Initialise memory to store cap table caps */
    ct_caps = (seL4_CPtr*)malloc(sizeof(seL4_CPtr) * CT_PAGES);
    memset(ct_caps, 0, sizeof(seL4_CPtr) * CT_PAGES);

    /*  */
 	seL4_ARM_VMAttributes vm_attr = 0;
    /* Loop variables */
 	int err = 0;
    seL4_Word ft_addr_current = ft_addr_start;
    seL4_Word ct_addr_current = ct_addr_start;
    /* Initialise the memory for the frame table and cap tables */
	while((ft_addr_current < ft_addr_end) && (ct_addr_current < ct_addr_end)){
        /* Get the caps for the frametable */
        if(*ft_caps == seL4_CapNull){
            /* Create the frame cap */	
            err = cspace_ut_retype_addr(ft_addr_current, seL4_ARM_SmallPageObject,
                                        seL4_PageBits, cur_cspace, ft_caps);
            conditional_panic(err, "Cannot create frame table cap\n");
            /* Map in the frame */
            err = map_page(*ft_caps, seL4_CapInitThreadPD, paddrToVaddr(ft_addr_current), 
                           seL4_AllRights, vm_attr);
            conditional_panic(err, "Cannot map frame table page\n");
        }    
        /* Get the caps for the cap table */
        if(*ct_caps == seL4_CapNull){
            /* Create the cap table cap */  
            err = cspace_ut_retype_addr(ct_addr_current, seL4_ARM_SmallPageObject,
                                        seL4_PageBits, cur_cspace, ct_caps);
            conditional_panic(err, "Cannot create cap table cap\n");
            /* Map in the cap table entry */
            err = map_page(*ct_caps, seL4_CapInitThreadPD, paddrToVaddr(ct_addr_current), 
                           seL4_AllRights, vm_attr);
            conditional_panic(err, "Cannot map cap table page\n");
        }
        /* Next */
        ft_addr_current += (1 << seL4_PageBits);
        ft_caps++;
        ct_addr_current += (1 << seL4_PageBits);
        ct_caps++;
    }

    for (int ftIndex = 0; ftIndex < FT_PAGES * ENTRIES_IN_PAGE; ftIndex++) {
        frametable[ftIndex] = FREE;
        frametable[ftIndex] |= (ftIndex + 1) << FTE_STATE_BITS;
    }

    
    *(int*)paddrToVaddr(ft_addr_start) = 0;
    return 0;
}
//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is retyped into a frame, 
//and the frame is mapped into the SOS window at a fixed offset of the physical address.
int frame_alloc() {
    /* Check frame table has been initialised */
    if (ft_addr_start == 0 || ct_addr_start == 0) {

    }
	return 0;
}
//frame_free: the physical memory is no longer mapped in the window, the frame object is destroyed, and the physical memory range is returned via ut_free.
int frame_free() {
	return 0;
}