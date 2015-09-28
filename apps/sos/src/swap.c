#include <sos/vmem_layout.h>
#include <mapping.h>
#include <nfs/nfs.h>
#include <setjmp.h>
#include "swap.h"
#include "frametable.h"
#include "pagetable.h"

#define SWAP_SLOTS 	(256 * 512)
#define IN_USE 		(1 << 31) 

seL4_Word swap_table[SWAP_SLOTS];
seL4_Word swap_head = -1;
fhandle_t *swap_handle = NULL;


typedef struct _swap_args {
    int index;
    int slot;
} swap_args;

typedef struct _swap_init_args {
	swap_args *args;
	int offset;
} swap_init_args;


void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);

void write_to_swap_slot (int index) {
	seL4_Word slot = swap_head;
	// We have reached the end of the swap table
	if (slot == SWAP_SLOTS) {
		// What do here?
	}
	// Get the next free swap slot
	swap_head = swap_table[slot];
	swap_table[slot] |= IN_USE;
	// Do the write to the slot at the offset
	int offset = slot * PAGE_SIZE;
	
	// 9242_TODO Rewrite when longjmp is used
	// This will probably longjmp back to frame_alloc, which will longjmp to whatever it was passed
	swap_args *args = malloc(sizeof(swap_args));
	args->index = index;
	args->slot = slot;
	int status = 0;
	if (swap_handle == NULL) {
		swap_handle = -1;
		swap_init_args *init_args = malloc(sizeof(swap_init_args));
		init_args->args = args;
		init_args->offset = offset;
		//status = nfs_lookup(&mnt_point, "swap", swap_init_cb, (uintptr_t)init_args);
	// Currently being initialised
	} else if (swap_handle == -1) {
		// Timer callback to write_to_swap_slot?
	} else {
		//status = nfs_write(swap_handle, offset, PAGE_SIZE, index_to_vaddr(index), swap_cb, (uintptr_t)args);
	}
	// Check if RPC succeeded
    if (status != RPC_OK) {

    }
}

void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	swap_handle = fh;
	swap_init_args *init_args = (swap_init_args *) token;
	status = nfs_write(swap_handle, init_args->offset, PAGE_SIZE, index_to_vaddr(init_args->args->index), swap_cb, (uintptr_t)init_args->args);
	if (status != RPC_OK) {
 		// Jmp back with error code?
    }
}

void swap_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
	swap_args* args = (swap_args*) token;
	int index = args->index;
	int slot = args->slot;
	// Get process mapping from frame
    int pid = (frametable[index].frame_status & PROCESS_MASK) >> PROCESS_BIT_SHIFT;
    // Store the slot for retrieval, mark the frame as swapped
    seL4_Word dir_index = PT_TOP(frametable[index].vaddr);
    seL4_Word page_index = PT_BOTTOM(frametable[index].vaddr);
    proc_table[pid]->page_directory[dir_index][page_index] = slot | SWAPPED;
    // Unmap from seL4 page directory and set addr_space page directory entry to swapped, and put the swap slot in the entry
    seL4_ARM_Page_Unmap(frametable[index].frame_cap);
    // 9242_TODO
    // Clear the frame
    // longjmp back?
}