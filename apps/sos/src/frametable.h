#include <sel4/types.h>
#include <limits.h>

#define FT_TOTAL_SIZE_BITS	21
#define FT_SIZE_BITS 		20
#define CT_SIZE_BITS 		20
#define ENTRIES_IN_PAGE		1024

#define FRAME_INDEX_MASK	0x000FFFFF
#define FRAME_UT_ALLOCED	(1 << 31)
#define FRAME_ALLOCED       (1 << 30)

/* Frame table entry bits */
#define FTE_STATE_BITS		3
#define FTE_STATE     		(0x00000007)
#define FTE_NEXT_FREE		(0xFFFFFFFF << FTE_STATE_BITS)

/* Frame table states */
#define FREE                0 

/*
 * This frametable allows for 256 pages (256*1024 entries) of frames.
 * This allows mapping for up to 1 GB of physical memory. However this is entirely dependent on how much untyped memory is available.
 */

int frame_init(seL4_Word low, seL4_Word high);

//frame_alloc: the physical memory is reserved via the ut_alloc, the memory is retyped into a frame, 
//and the frame is mapped into the SOS window at a fixed offset of the physical address.
int frame_alloc(void);

//frame_free: the physical memory is no longer mapped in the window, the frame object is destroyed, and the physical memory range is returned via ut_free.
int frame_free(void);