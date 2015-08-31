#ifndef FRAMETABLE_H
#define FRAMETABLE_H

#include <sel4/types.h>
#include <limits.h>
#include <sos/vmem_layout.h>
//"warnings"
#define FT_INITIALISED      1
#define FT_OK               0

//"errors"
#define FT_ERR              (-1)
#define FT_NOT_INITIALISED  (-2)
#define FT_NO_MEM           (-3)

// Alloc options
#define NOMAP				0
#define KMAP				1


typedef struct _ft_entry {
    seL4_Word frame_status;
    seL4_CPtr frame_cap;
} ft_entry;

//sos vspace addr of ft
ft_entry* frametable;

/*
 * This frametable allows for 256 pages (256*1024 entries) of frames.
 * This allows mapping for up to 1 GB of physical memory. However this is entirely dependent on how much untyped memory is available.
 */

int frame_init(void);

//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is retyped into a frame, 
//and the frame is mapped into the SOS window at a fixed offset of the physical address.
int frame_alloc(seL4_Word *vaddr, int map);

//frame_free: the physical memory is no longer mapped in the window, the frame object is destroyed, and the physical memory range is returned via ut_free.
int frame_free(int index);

seL4_Word index_to_paddr(int index);
seL4_Word index_to_vaddr(int index);

#endif
