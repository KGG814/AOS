#include <sos/vmem_layout.h>
#include <mapping.h>
#include <nfs/nfs.h>
#include "swap.h"
#include "frametable.h"

#define SWAP_SLOTS 	(256 * 512)
#define IN_USE 		(1 << 31) 

seL4_Word swap_table[SWAP_SLOTS];
seL4_Word swap_head = -1;
fhandle_t *swap_handle;

typedef struct _swap_args {
    int index;
    // jmp buffer
} swap_args;

void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);

int swap_init(void) {
	swap_head = 0;
	// 9242_TODO Open the swap file	
	// Spinlock on semaphore
	/*
	// Initialise  and wait on semaphore
	sync_sem_t swap_init_sem = sync_create_sem(0);
	int status = nfs_lookup(&mnt_point, "swap", swap_init_cb, &swap_init_sem);
    if (status != RPC_OK) {
        return 1;
    }
    // Wait for swap filehandle to be set
    sync_wait(swap_init_sem);
    return 0;
    */
}

void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	swap_handle = fh;
	// Signal semaphore
	// sync_signal(*token);
}

seL4_Word write_to_swap_slot (int index) {
	seL4_Word new_slot = swap_head;
	// We have reached the end of the swap table
	if (new_slot == SWAP_SLOTS) {
		return -1;
	}
	// Get the next free swap slot
	swap_head = swap_table[new_slot];
	swap_table[new_slot] |= IN_USE;
	// Do the write to the slot at the offset
	int offset = new_slot * PAGE_SIZE;
	/*
	// 9242_TODO Rewrite when longjmp is used
	// This will probably longjmp back to frame_alloc, which will logngjmp to whatever it was passed
	swap_args *args = malloc(sizeof(swap_args))
	int status = nfs_write(swap_handle, offset, PAGE_SIZE, index_to_vaddr(index), swap_cb, args);

    if (status != RPC_OK) {
 		// Jmp back with error code?
    }*/
	return new_slot;
}

void swap_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
	args = (swap_args) token;
	int index = args->index;
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