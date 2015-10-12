#include <sos/vmem_layout.h>
#include <mapping.h>
#include <nfs/nfs.h>
#include <setjmp.h>
#include "swap.h"
#include "frametable.h"
#include "pagetable.h"
#include "syscalls.h"
#include <clock/clock.h>
#include <string.h>
#include "debug.h"

#define SWAP_SLOTS 	(256 * 512)
#define IN_USE 		(1 << 31) 

extern fhandle_t mnt_point;
extern fattr_t *mnt_attr;
int buffer_head = -1;
int buffer_tail = -1;

seL4_Word swap_table[SWAP_SLOTS];
int swap_head = -1;
fhandle_t *swap_handle = NULL;

// Arguments needed for the swap nfs callback
typedef struct _swap_nfs_args {
    int index;
    int slot;
} swap_nfs_args;


// Swap write nfs callback
void swap_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);

// Swap initialisation callbacks
void swap_init_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_mnt_lookup_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
sattr_t get_new_swap_attributes(void);

// Swap read callbacks and nfs callback
void read_from_swap_slot_cb (int pid, seL4_CPtr reply_cap, frame_alloc_args *args);
void read_from_swap_slot_cb2 (int pid, seL4_CPtr reply_cap, read_swap_args *args);
void swap_read_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);

// Functions for managing swap buffer
int get_next_swap_slot(void);
void free_swap_slot(int slot);

// Write a given frame to the next free swap slot
void write_to_swap_slot (int pid, seL4_CPtr reply_cap, write_swap_args *args) {
	if (SOS_DEBUG) printf("write_to_swap_slot\n");
	// Check if we have initialised
	if (swap_handle == NULL) {
		// Swap file has not been initialised
		// Check if we have mnt attr to use for making the file
		if (mnt_attr != NULL) {
			// NFS call has been previously made, can use the already read mnt_attr
			// Get attributes for new swap file
			sattr_t swap_attr = get_new_swap_attributes();
			// Do the NFS call to create a swap file
            int status = nfs_create(&mnt_point, "swap", &swap_attr, swap_init_nfs_cb, (uintptr_t) args);
            // Check if the call succeeded
			if (status != RPC_OK) {
				assert(RTN_ON_FAIL);
				send_seL4_reply(reply_cap, -1);
			}
        } else {
        	// Haven't got the mount attributes for NFS yet, meaning this is the first NFS call
        	mnt_attr = malloc(sizeof(fattr_t));
        	// Malloc failed
        	if (mnt_attr == NULL) {
        		assert(RTN_ON_FAIL);
        		send_seL4_reply(reply_cap, -1);
        	} else {
        		// Read the current directory to get mnt_attr
        		int status = nfs_lookup(&mnt_point, ".", swap_mnt_lookup_nfs_cb, (uintptr_t) args);
	            if (status != RPC_OK) {
	            	send_seL4_reply(reply_cap, -1);
	            } 
        	}
        }
	} else {
		// Swap file has been initialised
		// Get all the arguments we use
		int index = args->index;
		printf("index %d\n", index);
		// Get the next free swap slot
		int slot = get_next_swap_slot();
		// Get the pid of the process owning the frame to be swapped
	    int swapped_frame_pid = (frametable[index].frame_status & PROCESS_MASK) >> PROCESS_BIT_SHIFT;
	    // Get the indices for the page table
	    seL4_Word dir_index = PT_TOP(frametable[index].vaddr);
	    seL4_Word page_index = PT_BOTTOM(frametable[index].vaddr);
	    // DEBUG
	    if (SOS_DEBUG) printf("vaddr for frame that will be swapped %p, pid :%d\n", 
	    	(void *) frametable[index].vaddr, swapped_frame_pid);
	    assert(frametable[index].vaddr != 0);
	    // Set the process's page table entry to swapped and store the swap slot
	    if (SOS_DEBUG) printf("Set vaddr %p to swapped\n", (void *) frametable[index].vaddr);
	    proc_table[swapped_frame_pid]->page_directory[dir_index][page_index] = slot | SWAPPED;
	    // Set the frame vaddr to 0, removing association with vmem
	    frametable[index].vaddr = 0;
		// Check if we have reached end of swap file
		if (slot == SWAP_SLOTS) {
			// We have reached the end of the swap table
			send_seL4_reply(reply_cap, -1);
		} else {	
			// Calculate offset into swap file
			int offset = slot * PAGE_SIZE;
			// Get kernel vaddr for frame
			char *addr = (void *) index_to_vaddr(index);
			// Initialise callback args
			args->slot = slot;
			args->offset = offset;
			args->addr = (seL4_Word) addr;
			args->bytes_written = 0;
			// DEBUG
			if (SOS_DEBUG) printf("Writing at slot %p, offset %p, index %p, address %p\n", (void *) slot, (void *) offset, (void *) index, addr);
			// Do the write to the slot at the offset
			int status = nfs_write(swap_handle, offset, PAGE_SIZE, addr, swap_write_nfs_cb, (uintptr_t)args);
			// Check if RPC succeeded
    		if (status != RPC_OK) {
    			assert(RTN_ON_FAIL);
    			send_seL4_reply(reply_cap, -1);
   			}
		}
	}
    if (SOS_DEBUG) printf("write_to_swap_slot ended\n");
}

// NFS callback for writing
void swap_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
	
	// Get the args struct form the token
	write_swap_args* args = (write_swap_args*) token;
	// Increment the write state so we're writing to the correct place in the swap file
	args->offset 		+= count;
	args->bytes_written += count;
	args->addr 			+= count;
	// Get the arguments we're using
	int index 			= args->index;
	int pid 			= args->pid;
	seL4_CPtr reply_cap = args->reply_cap;
	int slot   			= args->slot;
	int offset 		  	= args->offset;
	int bytes_written 	= args->bytes_written;
	seL4_Word addr 		= args->addr;
	
	if (SOS_DEBUG) printf("swap_write_nfs_cb, wrote %d, written %d, offset %p\n", 
		                  count, bytes_written, (void *)offset);
	// Check the NFS call worked as expected
	if (status != NFS_OK) {
		// NFS call failed
		assert(RTN_ON_FAIL);
		send_seL4_reply(reply_cap, -1);
		free(args);
	} else {
		if (args->bytes_written == PAGE_SIZE) {
			// Completed the write
			// Check for invalid args
		    if (index <= 0 || slot < 0 || slot >= SWAP_SLOTS) {
		    	assert(RTN_ON_FAIL);
		    	send_seL4_reply(reply_cap, -1);
		    	free(args);
		    } else {
		    	// Set the swapped out frame to invalid
		    	frametable[index].frame_status = 0;
		    	// Modify the frame alloc callback, as it doesn't need to kernel map a page being swapped out
			    frame_alloc_args *alloc_args = (frame_alloc_args *) args->cb_args;
			    alloc_args->map = NOMAP;
			    // Do the callback
			    args->cb(pid, reply_cap, args->cb_args);
			    free(args);
	    	}
		} else {
			// Haven't completed write, do another NFS call
			int rpc_status = nfs_write(swap_handle, offset, PAGE_SIZE - bytes_written, (void *) addr, swap_write_nfs_cb, (uintptr_t)args);
			// Check if call succeeded
			if (rpc_status != RPC_OK) {
				assert(RTN_ON_FAIL);
    			send_seL4_reply(reply_cap, -1);
    			free(args);
   			}
		}   
	}
	if (SOS_DEBUG) printf("swap_write_nfs_cb ended\n");
}

// NFS callback for mnt point lookup
void swap_mnt_lookup_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	if (SOS_DEBUG) printf("swap_mnt_lookup_nfs_cb\n");
	// Get all the arguments we use
    write_swap_args *write_args = (write_swap_args *) token;
    int pid = write_args->pid;
    seL4_CPtr reply_cap = write_args->reply_cap;
    // Check the NFS call worked as expected
	if (status != NFS_OK) {
        assert(RTN_ON_FAIL);
    }
	// Copy from the temp NFS buffer to mnt_attr
    memcpy(mnt_attr, fattr, sizeof(fattr_t));
    // Continue writing to the requested swap slot
    write_to_swap_slot(pid, reply_cap, write_args);
    if (SOS_DEBUG) printf("swap_mnt_lookup_nfs_cb ended\n");
}

// NFS callback for swap init
void swap_init_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	if (SOS_DEBUG) printf("swap_init_nfs_cb\n");
	// Get all the arguments we use
	write_swap_args *write_args = (write_swap_args *) token;
	int pid = write_args->pid;
	seL4_CPtr reply_cap = write_args->reply_cap;
	// Initialise the swap head index
	swap_head = 0;
	//Initalise swap table
	for (int i = 0; i < SWAP_SLOTS; i++) {
		// Each index points to the next slot
		swap_table[i] = i + 1;
	}
	// Check if the swap handle is NULL. It may not be NULL if another process is currently initialising
	if (swap_handle == NULL) {
		swap_handle = malloc(sizeof(fhandle_t));
		// Check malloc
		if (swap_handle == NULL) {
			// Malloc failed
			assert(RTN_ON_FAIL);
			send_seL4_reply(reply_cap, -1);
			return;
		} else {
			// Copy from temp NFS buffer to swap_handle
			memcpy(swap_handle, fh, sizeof(fhandle_t));
		}	
	}
	// Continue writing to the requested swap slot, if another process is initialising it will retry
	write_to_swap_slot(pid, reply_cap, write_args);
	if (SOS_DEBUG) printf("swap_init_nfs_cb ended\n");
}

// Read a page from the swap file
void read_from_swap_slot(int pid, seL4_CPtr reply_cap, read_swap_args *args) {
	if (SOS_DEBUG) printf("read_from_swap_slot\n");
	// Initialise arguments to frame alloc
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    alloc_args->map = KMAP;
    alloc_args->cb = (callback_ptr)read_from_swap_slot_cb;
    alloc_args->cb_args = args;
    // Allocate a frame to put the swapped in frame into
    frame_alloc_swap(pid, reply_cap, alloc_args);
	if (SOS_DEBUG) printf("read_from_swap_slot ended\n");
}

// First callback for reading from the swap file
// frame haw now been allocated so swap buffer must be manipulated
void read_from_swap_slot_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args) {
	if (SOS_DEBUG) printf("read_from_swap_slot_cb\n");
    read_swap_args *read_args = (read_swap_args *)args->cb_args;
    // Copy the index from the frame_alloc return vars to the read args
    read_args->index = args->index;
    // Get the arguments we're using
    int index = read_args->index;
    seL4_Word vaddr = read_args->vaddr;
    // Get page directory entries
    int dir_index = PT_TOP(vaddr);
    int page_index = PT_BOTTOM(vaddr);
    // Free the args from the frame_alloc call
    free(args);
    if (SOS_DEBUG) printf("Checking vaddr %p is swapped\n", (void *) vaddr);
    // Make sure the vaddr we're trying to handle is actually swapped out still
    assert(proc_table[pid]->page_directory[dir_index][page_index] & SWAPPED);
    // Get the swap slot from the page table
    read_args->slot =  proc_table[pid]->page_directory[dir_index][page_index] & SWAP_SLOT_MASK;
    // Put the mapping for the frame into the pagetable
    proc_table[pid]->page_directory[dir_index][page_index] = index;
	// Check if the frame is in the swap buffer
    if ((frametable[index].frame_status & SWAP_BUFFER_MASK)) {
    	// Frame is in swap buffer, all we need to do is set the pid and status bits
    	// Clear the pid
    	frametable[index].frame_status &= ~PROCESS_MASK;
    	// Set the frame to used and the pid
        frametable[index].frame_status |= FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT);

    } else {
    	// Not in swap buffer
        // Set the tail to point to the new frame
        frametable[buffer_tail].frame_status &= ~SWAP_BUFFER_MASK;
        frametable[buffer_tail].frame_status |= index;
        // Set the tail to the new frame
        buffer_tail = index;
        // Set status in new frame
        frametable[index].frame_status = FRAME_IN_USE | (pid << PROCESS_BIT_SHIFT) | buffer_head;
    }
    
    sos_map_page_swap(read_args->index, vaddr, pid, reply_cap, (callback_ptr)read_from_swap_slot_cb2,
                      read_args);
    if (SOS_DEBUG) printf("read_from_swap_slot_cb ended\n");

}

// Second callback for reading from swap file
// Page has been mapped in, so we can read from the file
void read_from_swap_slot_cb2 (int pid, seL4_CPtr reply_cap, read_swap_args *args) {
	if (SOS_DEBUG) printf("read_from_swap_slot_cb2\n");
	// Get the arguments we're using
	int offset = args->slot * PAGE_SIZE;
	// Set args for callback
	args->offset = offset;
	args->bytes_read = 0;
	// Do NFS callback
	int status = nfs_read(swap_handle, offset, PAGE_SIZE, swap_read_nfs_cb, (uintptr_t)args);
	// Check if RPC succeeded
	if (status != RPC_OK) {
		send_seL4_reply(reply_cap, -1);
	}
	if (SOS_DEBUG) printf("read_from_swap_slot_cb2 ended\n");
}

// NFS callback for reading from swap file
// File data is copied to memory
void swap_read_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data) {
	if (SOS_DEBUG) printf("swap_read_nfs_cb\n");
	// Get the args struct form the token
	read_swap_args *read_args = (read_swap_args *) token;
	// Increment the read state so we're reading from the correct place in the swap file
	read_args->offset += count;
	read_args->bytes_read += count;
	// Get the arguments we're using
	int index 				= read_args->index;
	int pid 				= read_args->pid;
	seL4_CPtr reply_cap 	= read_args->reply_cap;
	int slot 				= read_args->slot;
	int offset				= read_args->offset;
	int bytes_read 			= read_args->bytes_read;
	assert(count != 0);
	// Check if NFS call failed
	if (status != NFS_OK) {
		// NFS failed, return
		assert(1==0);
		send_seL4_reply(reply_cap, -1);
	} else {
		// NFS call succeeded
		// Copy from the temporary NFS buffer to memory
		printf("index %d\n", index);
		memcpy((void *) index_to_vaddr(index), data, count);
		// Check if we are done
		if (bytes_read == PAGE_SIZE) {
			// Finished reading pahe from swap file
			// Flush old instruction page from icache
			seL4_ARM_Page_Unify_Instruction(frametable[index].mapping_cap, 0, PAGESIZE);
			// Free swap slot
			free_swap_slot(slot);
			// Do callback
			read_args->cb(pid, reply_cap, read_args->cb_args);
			free(read_args);
		} else {
			// Still reading from swap file
			read_args->vaddr += count;
			int rpc_status = nfs_read(swap_handle, offset, PAGE_SIZE - bytes_read, swap_read_nfs_cb, (uintptr_t)read_args);
			// Check if RPC succeeded
			if (rpc_status != RPC_OK) {
				send_seL4_reply(reply_cap, -1);
				free(read_args);
			}
		}
	}
	if (SOS_DEBUG) printf("swap_read_nfs_cb ended\n");	
}

// Default attributes for a new swap file
sattr_t get_new_swap_attributes(void) {
	timestamp_t cur_time = time_stamp();
	// Attributes for a new swap file
	sattr_t swap_attr= {.mode = 0666
        		,.uid = mnt_attr->uid
            	,.gid = mnt_attr->gid
            	,.size = 0
            	,.atime = {.seconds = cur_time/1000000
                		   ,.useconds = cur_time % 1000000 
            	          }
        		,.mtime = {.seconds = cur_time/1000000
            		       ,.useconds = cur_time % 1000000
        			      }            
    			};
    return swap_attr;
}

// Use swap table to get next swap slot free in the swap file
int get_next_swap_slot(void) {
	// Get the next free swap slot
	int slot = swap_head;
	if (slot == SWAP_SLOTS) {
		return SWAP_SLOTS;
	}
	swap_head = swap_table[slot];
	// Setting it to in use
	swap_table[slot] = IN_USE;
	return slot;
}

// Free a swap slot from the swap table
void free_swap_slot(int slot) {
	swap_table[slot] = swap_head;
	swap_head = slot;
}