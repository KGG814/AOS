#include <sos/vmem_layout.h>
#include <mapping.h>
#include <nfs/nfs.h>
#include <setjmp.h>
#include "swap.h"
#include "frametable.h"
#include "pagetable.h"
#include "syscalls.h"

#define SWAP_SLOTS 	(256 * 512)
#define IN_USE 		(1 << 31) 

extern fhandle_t mnt_point;

seL4_Word swap_table[SWAP_SLOTS];
seL4_Word swap_head = -1;
fhandle_t *swap_handle = NULL;


typedef struct _swap_nfs_args {
    int index;
    int slot;
} swap_nfs_args;

typedef struct _swap_init_args {
	write_swap_args *args;
} swap_init_args;


void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);

void write_to_swap_slot (int pid, seL4_CPtr reply_cap, void *args) {
	// Get the next free swap slot
	seL4_Word slot = swap_head;
	swap_head = swap_table[slot];
	// Setup callback args
	write_swap_args *write_args = (write_swap_args *) args;
	write_args->slot = slot;
	// We have reached the end of the swap table
	if (slot == SWAP_SLOTS) {
		send_seL4_reply(reply_cap, -1);
	}
	
	swap_table[slot] |= IN_USE;
	// Do the write to the slot at the offset
	int offset = slot * PAGE_SIZE;
	
	int status = 0;
	if (swap_handle == NULL) {
		swap_handle = (fhandle_t *)-1;
		status = nfs_lookup(&mnt_point, "swap", swap_init_cb, (uintptr_t)args);
	// Currently being initialised
	} else if (swap_handle == (fhandle_t *)-1) {
		// Timer callback to write_to_swap_slot?
	} else {
		status = nfs_write(swap_handle, offset, PAGE_SIZE, (void *)index_to_vaddr(write_args->index), swap_cb, (uintptr_t)args);
	}
	// Check if RPC succeeded
    if (status != RPC_OK) {

    }
}

void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	swap_handle = fh;
	write_swap_args *write_args = (write_swap_args *) token;
	int offset = write_args->slot * PAGE_SIZE;
	status = nfs_write(swap_handle, offset, PAGE_SIZE, (void *)index_to_vaddr(write_args->index), swap_cb, token);
	if (status != RPC_OK) {
 		// Jmp back with error code?
    }
}

void swap_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
	write_swap_args* args = (write_swap_args*) token;
	if (status != NFS_OK) {
		send_seL4_reply(args->reply_cap, -1);
		free(args);
	} else {
		int index = args->index;
		int slot = args->slot;
		// Get process mapping from frame
	    int swapped_frame_pid = (frametable[index].frame_status & PROCESS_MASK) >> PROCESS_BIT_SHIFT;
	    // Store the slot for retrieval, mark the frame as swapped
	    seL4_Word dir_index = PT_TOP(frametable[index].vaddr);
	    seL4_Word page_index = PT_BOTTOM(frametable[index].vaddr);
	    // Unmap from seL4 page directory and set addr_space page directory entry to swapped, and put the swap slot in the entry
	    proc_table[swapped_frame_pid]->page_directory[dir_index][page_index] = slot | SWAPPED;
	    seL4_ARM_Page_Unmap(frametable[index].frame_cap);
	    args->cb(args->pid, args->reply_cap, args->cb_args);
	    free(args);
	}
	
}