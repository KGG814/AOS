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

#define PROCESS_MASK        0x0FF00000
#define SWAP_BUFFER_MASK	0x000FFFFF
#define STATUS_MASK			0xF0000000
#define PROCESS_BIT_SHIFT   20
/* Mask to obtain frame index */
#define FRAME_INDEX_MASK	0x000FFFFF

// Alloc options
#define NOMAP				0
#define KMAP				1


typedef struct _ft_entry {
    seL4_Word frame_status;
    seL4_CPtr frame_cap;
    seL4_Word vaddr;
} ft_entry;

//sos vspace addr of ft
ft_entry* frametable;

int frame_init(void);

//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is retyped into a frame, 
//and the frame is mapped into the SOS window at a fixed offset of the physical address.
int frame_alloc(seL4_Word *vaddr, int map, int pid);
int frame_alloc_swap(seL4_Word *vaddr, int map, int pid);

//frame_free: the physical memory is no longer mapped in the window, the frame object is destroyed, and the physical memory range is returned via ut_free.
int frame_free(int index);

seL4_Word index_to_paddr(int index);
seL4_Word index_to_vaddr(int index);

#endif
