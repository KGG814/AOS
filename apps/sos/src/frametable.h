#ifndef FRAMETABLE_H
#define FRAMETABLE_H

#include <sel4/types.h>
#include <limits.h>
#include <sos/vmem_layout.h>
//"warnings"
#define FRAMETABLE_INITIALISED      1
#define FRAMETABLE_OK               0

//"errors"
#define FRAMETABLE_ERR              (-1)
#define FRAMETABLE_NOT_INITIALISED  (-2)
#define FRAMETABLE_NO_MEM           (-3)
#define FRAMETABLE_CALLBACK			(-4)

/* Frame status bits */
#define FRAME_INVALID    	(1 << 31) //not managed by our frame table 
#define FRAME_IN_USE        (1 << 30) //frame is in use and managed by us
#define FRAME_DONT_SWAP     (1 << 29) //frame is not to be swapped
//this bit is 1 if the frame should be swapped on the next pass of the clock
#define FRAME_SWAP_MARKED   (1 << 28)

#define PROCESS_MASK        0x0FF00000
#define SWAP_BUFFER_MASK	0x000FFFFF
#define STATUS_MASK			0xF0000000
#define PROCESS_BIT_SHIFT   20
/* Mask to obtain frame index */
#define FRAME_INDEX_MASK	0x000FFFFF

// Alloc options
#define NOMAP				0
#define KMAP				1

typedef void (*callback_ptr)(int, seL4_CPtr, void*);

typedef struct _ft_entry {
    seL4_Word frame_status;
    seL4_CPtr frame_cap;
    seL4_Word vaddr;
    seL4_CPtr mapping_cap;
} ft_entry;

typedef struct _frame_alloc_args {
	int map;
	callback_ptr cb;
	void *cb_args;
	// Not initialised
	seL4_Word vaddr;
	seL4_Word pt_addr;
	int index;
} frame_alloc_args;


//sos vspace addr of ft
ft_entry* frametable;

int frame_init(void);

//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is retyped into a frame, 
//and the frame is mapped into the SOS window at a fixed offset of the physical address.
int frame_alloc(seL4_Word *vaddr, int map, int pid);
void frame_alloc_swap(int pid, seL4_CPtr reply_cap, void *args);

//frame_free: the physical memory is no longer mapped in the window, the frame object is destroyed, and the physical memory range is returned via ut_free.
int frame_free(int index);

seL4_Word index_to_paddr(int index);
seL4_Word index_to_vaddr(int index);
int vaddr_to_index(seL4_Word vaddr);

#endif
