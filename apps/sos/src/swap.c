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



// Swap initialisation callbacks
void swap_init_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
sattr_t get_new_swap_attributes(void);

// Swap write nfs callback
void swap_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);

// Swap read callbacks and nfs callback
void read_from_swap_slot_cb (int pid, seL4_CPtr reply_cap, void *args);
void read_from_swap_slot_cb_continue (int pid, seL4_CPtr reply_cap, void *args);
void swap_read_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);

// Functions for managing swap buffer
int get_next_swap_slot(void);
int free_swap_slot(int slot);

// Write a given frame to the next free swap slot
void write_to_swap_slot (int pid, seL4_CPtr reply_cap, void *args) {
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
		// Haven't got the mount attributes for NFS yet, meaning this is the first NFS call
        } else {
        	mnt_attr = malloc(sizeof(fattr_t));
        	// Malloc failed
        	if (mnt_attr == NULL) {
        		assert(RTN_ON_FAIL);
        		send_seL4_reply(reply_cap, -1);
        	} else {
        		// Read the current directory to get mnt_attr
        		int status = nfs_lookup(&mnt_point, ".", swap_mnt_lookup_cb, (uintptr_t) args);
	            if (status != RPC_OK) {
	            	send_seL4_reply(reply_cap, -1);
	            } 
        	}
        }
	} else {
		// Get all the arguments we use
		write_swap_args *write_args = (write_swap_args *) args;
		int index = write_args->index;
		// Get the next free swap slot
		int slot = get_next_swap_slot();
		// Get the pid of the process owning the frame to be swapped
	    int swapped_frame_pid = (frametable[index].frame_status & PROCESS_MASK) >> PROCESS_BIT_SHIFT;
	    // Get the indices for the page table
	    seL4_Word dir_index = PT_TOP(frametable[index].vaddr);
	    seL4_Word page_index = PT_BOTTOM(frametable[index].vaddr);
	    // DEBUG
	    if (SOS_DEBUG) printf("vaddr for frame that will be swapped %p\n", (void *) frametable[index].vaddr);
	    assert(frametable[index].vaddr != 0);
	    // Flush in case it is an instruction frame
	    seL4_ARM_Page_Unify_Instruction(frametable[index].frame_cap, 0, PAGESIZE);
	    // Set the process's page table entry to swapped and store the swap slot
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
			write_args->slot = slot;
			write_args->offset = offset;
			write_args->addr = (seL4_Word) addr;
			write_args->bytes_written = 0;
			// DEBUG
			if (SOS_DEBUG) printf("Writing at slot %p, offset %p, index %p, address %p\n", (void *) slot, (void *) offset, (void *) index, addr);
			// Do the write to the slot at the offset
			int status = nfs_write(swap_handle, offset, PAGE_SIZE, addr, swap_write_nfs_cb, (uintptr_t)write_args);
			// Check if RPC succeeded
    		if (status != RPC_OK) {
    			assert(RTN_ON_FAIL);
    			send_seL4_reply(reply_cap, -1);
   			}
		}
	}
    if (SOS_DEBUG) printf("write_to_swap_slot ended\n");
}

void swap_mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	if (SOS_DEBUG) printf("swap_mnt_lookup_cb\n");
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
    if (SOS_DEBUG) printf("swap_mnt_lookup_cb ended\n");
}

void swap_init_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	if (SOS_DEBUG) printf("swap_init_nfs_cb\n");
	// Initialise the swap head index
	swap_head = 0;
	//Initalise swap table
	for (int i = 0; i < SWAP_SLOTS; i++) {
		// Each index points to the next slot
		swap_table[i] = i + 1;
	}
	// Get all the arguments we use
	write_swap_args *write_args = (write_swap_args *) token;
	int pid = write_args->pid;
	seL4_CPtr reply_cap = write_args->reply_cap;
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
	// Continue writing to the requested swap slot
	write_to_swap_slot(pid, reply_cap, write_args);
	if (SOS_DEBUG) printf("swap_init_nfs_cb ended\n");
}

void swap_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
	if (SOS_DEBUG) printf("swap_write_nfs_cb, wrote %d\n", count);
	write_swap_args* args = (write_swap_args*) token;
	// Increment the write state so we're writing to the corrent place in the swap file
	int index = args->index;
	args->offset += count;
	args->bytes_written += count;
	args->addr += count;
	// Get the arguments we're using
	int slot = args->slot;
	// Check the NFS call worked as expected
	if (status != NFS_OK) {
		assert(RTN_ON_FAIL);
		send_seL4_reply(args->reply_cap, -1);
		free(args);
	} else {
		if (args->bytes_written == PAGE_SIZE) {
			// Completed the write
			// Check for invalid args
		    if (index <= 0 || slot < 0 || slot >= SWAP_SLOTS) {
		    	assert(RTN_ON_FAIL);
		    	send_seL4_reply(args->reply_cap, -1);
		    	free(args);
		    } else {
		    	// Set the swapped out frame to invalid
		    	frametable[index].frame_status = 0;
		    	// Modify the frame alloc callback, as it doesn't need to kernel map a page being swapped out
			    frame_alloc_args *alloc_args = (frame_alloc_args *) args->cb_args;
			    alloc_args->map = NOMAP;
			    // Do the callback
			    args->cb(args->pid, args->reply_cap, args->cb_args);
			    free(args);
	    	}
		} else {
			// Haven't completed write, do another NFS call
			int rpc_status = nfs_write(swap_handle, args->offset, PAGE_SIZE - args->bytes_written, (void *) args->addr, swap_write_nfs_cb, (uintptr_t)args);
			if (rpc_status != RPC_OK) {
				assert(RTN_ON_FAIL);
    			send_seL4_reply(args->reply_cap, -1);
    			free(args);
   			}
		}   
	}
	if (SOS_DEBUG) printf("swap_write_nfs_cb ended\n");
}



void read_from_swap_slot (int pid, seL4_CPtr reply_cap, void *args) {
	if (SOS_DEBUG) printf("read_from_swap_slot\n");
	read_swap_args *read_args = (read_swap_args *) args;
	// Initialise arguments to frame alloc
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    alloc_args->map = NOMAP;
    alloc_args->cb = read_from_swap_slot_cb;
    alloc_args->cb_args = args;
    // Allocate a frame to put the swapped in frame into
    frame_alloc_swap(pid, reply_cap, alloc_args);
	if (SOS_DEBUG) printf("read_from_swap_slot ended\n");
}

void read_from_swap_slot_cb (int pid, seL4_CPtr reply_cap, void *args) {
	if (SOS_DEBUG) printf("read_from_swap_slot_cb\n");
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    read_swap_args *read_args = alloc_args->cb_args;
    // Get page directory entries
    int dir_index = PT_TOP(read_args->vaddr);
    int page_index = PT_BOTTOM(read_args->vaddr);
    // Copy the index from the frame_alloc return vars to the read args
    read_args->index = alloc_args->index;
    // Free the args from the frame_alloc call
    free(alloc_args);
    // Get the arguments we're using
    int index = read_args->index;
    // Make sure the vaddr we're trying to handle is actually swapped out still
    assert(proc_table[pid]->page_directory[dir_index][page_index] & SWAPPED);
    // Get the swap slot from the page table
    read_args->slot =  proc_table[pid]->page_directory[dir_index][page_index] & SWAP_SLOT_MASK;
    // Put the mapping for the frame into the pagetable
    proc_table[pid]->page_directory[dir_index][page_index] = index;
	// Check if the frame is in the swap buffer
    if ((frametable[read_args->index].frame_status & SWAP_BUFFER_MASK)) {
    	// Frame is in swap buffer, all we need to do is set the pid and status bits
    	// Clear the pid
    	frametable[read_args->index].frame_status &= ~PROCESS_MASK;
    	// Set the frame to used and the pid
        frametable[read_args->index].frame_status |= FRAME_IN_USE | (read_args->pid << PROCESS_BIT_SHIFT);

    } else {
    	// Not in swap buffer
        // Set the tail to point to the new frame
        frametable[buffer_tail].frame_status &= ~SWAP_BUFFER_MASK;
        frametable[buffer_tail].frame_status |= read_args->index;
        // Set the tail to the new frame
        buffer_tail = read_args->index;
        // Set status in new frame
        frametable[read_args->index].frame_status = FRAME_IN_USE | (read_args->pid << PROCESS_BIT_SHIFT) | buffer_head;
    }
    
    seL4_CPtr temp;
    sos_map_page_swap(read_args->index, read_args->vaddr, proc_table[pid]->vroot, 
                      proc_table[pid], pid, reply_cap, read_from_swap_slot_cb_continue,
                      read_args, &temp);
    if (SOS_DEBUG) printf("read_from_swap_slot_cb ended\n");

}

void read_from_swap_slot_cb_continue (int pid, seL4_CPtr reply_cap, void *args) {
	if (SOS_DEBUG) printf("read_from_swap_slot_cb_continue\n");
    read_swap_args *read_args = (read_swap_args *) args;
	int offset = read_args->slot * PAGE_SIZE;
	read_args->offset = offset;
	read_args->bytes_read = 0;
	int status = nfs_read(swap_handle, offset, PAGE_SIZE, swap_read_nfs_cb, (uintptr_t)args);
	// Check if RPC succeeded
	if (status != RPC_OK) {
		send_seL4_reply(reply_cap, -1);
	}
	if (SOS_DEBUG) printf("read_from_swap_slot_cb_continue ended\n");
}

void swap_read_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data) {
	if (SOS_DEBUG) printf("swap_read_nfs_cb\n");
	read_swap_args *read_args = (read_swap_args *) token;
	read_args->bytes_read += count;
	read_args->offset += count;
	assert(count != 0);
	if (status != NFS_OK) {
		assert(1==0);
		send_seL4_reply(read_args->reply_cap, -1);
	} else {
		memcpy((void *) index_to_vaddr(read_args->index), data, count);
		if (read_args->bytes_read == PAGE_SIZE) {
			// Free swap slot
			free_swap_slot(read_args->slot);
			read_args->cb(read_args->pid, read_args->reply_cap, read_args->cb_args);
			free(read_args);
		} else {
			read_args->vaddr += count;
			int rpc_status = nfs_read(swap_handle, read_args->offset, PAGE_SIZE - read_args->bytes_read, swap_read_nfs_cb, (uintptr_t)read_args);
			// Check if RPC succeeded
			if (rpc_status != RPC_OK) {
				send_seL4_reply(read_args->reply_cap, -1);
				free(read_args);
			}
		}
	}
	if (SOS_DEBUG) printf("swap_read_nfs_cb ended\n");	
}

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

int free_swap_slot(int slot) {
	swap_table[slot] = swap_head;
	swap_head = slot;
}