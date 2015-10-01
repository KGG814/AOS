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

#define SWAP_SLOTS 	(256 * 512)
#define IN_USE 		(1 << 31) 

extern fhandle_t mnt_point;
extern fattr_t *mnt_attr;

seL4_Word swap_table[SWAP_SLOTS];
int swap_head = -1;
fhandle_t *swap_handle = NULL;


typedef struct _swap_nfs_args {
    int index;
    int slot;
} swap_nfs_args;

typedef struct _swap_init_args {
	write_swap_args *args;
} swap_init_args;


void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);


void write_to_swap_slot (int pid, seL4_CPtr reply_cap, void *args) {
	printf("write_to_swap_slot\n");
	
	
	// Setup callback args
	int status = 0;

	if (swap_handle == NULL) {
		timestamp_t cur_time = time_stamp();
		printf("Swap handle is null\n");
		if (mnt_attr != NULL) {
			printf("Making swap file\n");
			sattr_t sattr = {.mode = 0666
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
            status = nfs_create(&mnt_point, "swap", &sattr, swap_init_cb, (uintptr_t)args);
			if (status != RPC_OK) {
				assert(1==0);
			} else {
			}
        } else {
        	printf("Getting mnt, mnt_point: %p\n", &mnt_point);
        	mnt_attr = malloc(sizeof(fattr_t));
        	int status = nfs_lookup(&mnt_point, ".", swap_mnt_lookup_cb, (uintptr_t) args);
            if (status != RPC_OK) {
            	assert(1==0);
            } else {
            }
        }
	} else {
		// Get the next free swap slot
		int slot = swap_head;
		// We have reached the end of the swap table
		if (slot == SWAP_SLOTS) {
			send_seL4_reply(reply_cap, -1);
		} else {	
			printf("Swap head was at %d\n", swap_head);
			swap_head = swap_table[slot];
			// Setting it to in use
			swap_table[slot] = IN_USE;
			// Put slot in args
			write_swap_args *write_args = (write_swap_args *) args;
			write_args->slot = slot;
			// Do the write to the slot at the offset
			int offset = slot * PAGE_SIZE;
			status = nfs_write(swap_handle, offset, PAGE_SIZE, (void *)index_to_vaddr(write_args->index), swap_cb, (uintptr_t)args);
		}
	}
	// Check if RPC succeeded
    if (status != RPC_OK) {

    }
}

void swap_mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	printf("\n\nswap_mnt_lookup_cb called\n\n\n");
    write_swap_args *write_args = (write_swap_args *) token;
    if (status != NFS_OK) {
        printf("Something went wrong\n");
    }
    printf("Before memcpy\n");
    memcpy(mnt_attr, fattr, sizeof(fattr_t));
    printf("After memcpy\n");
    write_to_swap_slot(write_args->pid, write_args->reply_cap, write_args);
}

void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	printf("swap_init_cb called\n");
	swap_handle = fh;
	write_swap_args *write_args = (write_swap_args *) token;
	int offset = write_args->slot * PAGE_SIZE;
	rpc_stat_t rpc_status = nfs_write(swap_handle, offset, PAGE_SIZE, (void *)index_to_vaddr(write_args->index), swap_cb, token);
	if (rpc_status != RPC_OK) {
 		// Jmp back with error code?
    }
}

void swap_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
	printf("swap_cb called\n");
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
	    printf("Swapping frame at index %d, frame_status: %p, pid: %d\n", index, frametable[index].frame_status, args->pid);
	    if (swapped_frame_pid == 0) {
	    	send_seL4_reply(args->reply_cap, -1);
	    } else {
		    proc_table[swapped_frame_pid]->page_directory[dir_index][page_index] = slot | SWAPPED;
		    seL4_ARM_Page_Unmap(frametable[index].frame_cap);
		     args->cb(args->pid, args->reply_cap, args->cb_args);
	    }
	    free(args);
	}
	
}