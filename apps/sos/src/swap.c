#include <sos/vmem_layout.h>
#include <mapping.h>
#include "swap.h"
#include "frametable.h"

#define SWAP_SLOTS 	(256 * 512)
#define IN_USE 		(1 << 31) 

seL4_Word *swap_table;
seL4_Word swap_head;

int swap_init(void) {
	swap_head = 0;
	swap_table = SWAP_TABLE_START;
	seL4_Word curr_addr = (seL4_Word) swap_table;
	seL4_Word temp;
	int ft_index;
	seL4_ARM_VMAttributes vm_attr = 0;
	for (int curr_slot = 0; curr_slot < SWAP_SLOTS; curr_slot += PAGE_SIZE / sizeof(seL4_Word)) {
		ft_index = frame_alloc(&temp, NOMAP, 0);
		map_page(frametable[ft_index].frame_cap, seL4_CapInitThreadPD, curr_addr, seL4_AllRights, vm_attr);
		curr_addr += PAGE_SIZE;
		for (int i = 0; i < PAGE_SIZE; i+= sizeof(seL4_Word)) {
			swap_table[curr_slot + i] = curr_slot + i;
		}
	}
}

seL4_Word write_to_swap_slot (void) {
	seL4_Word new_slot = swap_head;
	// We have reached the end of the swap table
	if (new_slot == SWAP_SLOTS) {
		return -1;
	}
	// Get the next free swap slot
	swap_head = swap_table[new_slot];
	swap_table[new_slot] |= IN_USE;
	// 9242_TODO Do the write to the slot at the offset
	int offset = new_slot * PAGE_SIZE;
	return new_slot;
}