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
extern int buffer_head;
extern int buffer_tail;

seL4_Word swap_table[SWAP_SLOTS];
int swap_head = -1;
fhandle_t *swap_handle = NULL;


typedef struct _swap_nfs_args {
    int index;
    int slot;
} swap_nfs_args;



void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void swap_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);
int get_next_swap_slot(void);
int free_swap_slot(int slot);

void read_from_swap_slot_cb (int pid, seL4_CPtr reply_cap, void *args);
void read_from_swap_slot_cb_continue (int pid, seL4_CPtr reply_cap, void *args);
void swap_read_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);

void write_to_swap_slot (int pid, seL4_CPtr reply_cap, void *args) {
	printf("write_to_swap_slot\n");
	
	
	// Setup callback args
	int status = 0;

	if (swap_handle == NULL) {
		timestamp_t cur_time = time_stamp();
		if (mnt_attr != NULL) {
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
				send_seL4_reply(reply_cap, -1);
			}
        } else {
        	printf("Getting mnt, mnt_point: %p\n", &mnt_point);
        	mnt_attr = malloc(sizeof(fattr_t));
        	if (mnt_attr == NULL) {
        		send_seL4_reply(reply_cap, -1);
        	} else {
        		int status = nfs_lookup(&mnt_point, ".", swap_mnt_lookup_cb, (uintptr_t) args);
	            if (status != RPC_OK) {
	            	send_seL4_reply(reply_cap, -1);
	            } 
        	}
        	
        }
	} else {
		write_swap_args *write_args = (write_swap_args *) args;
		// Get the next free swap slot
		int slot = get_next_swap_slot();
	    int swapped_frame_pid = (frametable[write_args->index].frame_status & PROCESS_MASK) >> PROCESS_BIT_SHIFT;
	    // Store the slot for retrieval, mark the frame as swapped
	    seL4_Word dir_index = PT_TOP(frametable[write_args->index].vaddr);
	    seL4_Word page_index = PT_BOTTOM(frametable[write_args->index].vaddr);
	    printf("vaddr for frame that will be swapped %p\n", (void *) frametable[write_args->index].vaddr);
	    printf("dir_index: %d, page_index: %d pid: %d\n", dir_index, page_index, swapped_frame_pid);
	    assert(frametable[write_args->index].vaddr != 0);
	    seL4_ARM_Page_Unify_Instruction(frametable[write_args->index].frame_cap, 0, PAGESIZE);
	    proc_table[swapped_frame_pid]->page_directory[dir_index][page_index] = slot | SWAPPED;

	    frametable[write_args->index].vaddr = 0;
		// We have reached the end of the swap table
		if (slot == SWAP_SLOTS) {
			send_seL4_reply(reply_cap, -1);
		} else {	
			// Put slot in args
			write_swap_args *write_args = (write_swap_args *) args;
			write_args->slot = slot;
			// Do the write to the slot at the offset
			int offset = slot * PAGE_SIZE;
			char *addr = (void *) index_to_vaddr(write_args->index);
			printf("Writing at slot %p, offset %p, index %p, address %p\n", (void *) slot, (void *) offset, (void *) write_args->index, addr);
			write_args->offset = offset;
			write_args->addr = (seL4_Word) addr;
			write_args->bytes_written = 0;
			status = nfs_write(swap_handle, offset, PAGE_SIZE, addr, swap_write_nfs_cb, (uintptr_t)args);
			// Check if RPC succeeded
    		if (status != RPC_OK) {
    			send_seL4_reply(reply_cap, -1);
   			}
		}
	}
	
    printf("write_to_swap_slot ended\n");
}

void swap_mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	printf("swap_mnt_lookup_cb\n");
    write_swap_args *write_args = (write_swap_args *) token;
    if (status != NFS_OK) {
        printf("Something went wrong\n");
    }
    memcpy(mnt_attr, fattr, sizeof(fattr_t));
    write_to_swap_slot(write_args->pid, write_args->reply_cap, write_args);
    printf("swap_mnt_lookup_cb ended\n");
}

void swap_init_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
	printf("swap_init_cb\n");
	swap_head = 0;
	//Initalise swap table
	for (int i = 0; i < SWAP_SLOTS; i++) {
		swap_table[i] = i + 1;
	}
	write_swap_args *write_args = (write_swap_args *) token;
	if (swap_handle == NULL) {
		swap_handle = malloc(sizeof(fhandle_t));
		if (swap_handle == NULL) {
			send_seL4_reply(write_args->reply_cap, -1);
			return;
		} else {
			memcpy(swap_handle, fh, sizeof(fhandle_t));
			
		}	
	}
	write_to_swap_slot(write_args->pid, write_args->reply_cap, write_args);
	printf("swap_init_cb ended\n");
}

void swap_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
	printf("swap_write_nfs_cb, wrote %d\n", count);
	write_swap_args* args = (write_swap_args*) token;
	args->bytes_written += count;
	args->offset += count;
	args->addr += count;
	if (status != NFS_OK) {
		printf("Something went wrong %d\n", status);
		send_seL4_reply(args->reply_cap, -1);
		free(args);
	} else {
		if (args->bytes_written == PAGE_SIZE) {
			int index = args->index;
			int slot = args->slot;
			//printf("Swapped into slot %d\n", slot);
		    //printf("Swapped frame at index %p, frame_status: %p, pid: %d\n",(void *) index, (void *) frametable[index].frame_status, args->pid);
		    if (index <= 0 || slot < 0 || slot >= SWAP_SLOTS) {
		    	send_seL4_reply(args->reply_cap, -1);
		    	free(args);
		    } else {
		    	frametable[index].frame_status = 0;
			    frame_alloc_args *alloc_args = (frame_alloc_args *) args->cb_args;
			    alloc_args->map = NOMAP;
			    args->cb(args->pid, args->reply_cap, args->cb_args);
			    free(args);
	    	}
		} else {
			int rpc_status = nfs_write(swap_handle, args->offset, PAGE_SIZE - args->bytes_written, (void *) args->addr, swap_write_nfs_cb, (uintptr_t)args);
			if (rpc_status != RPC_OK) {
    			send_seL4_reply(args->reply_cap, -1);
   			}
		}   
	}
	printf("swap_write_nfs_cb ended\n");
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

void read_from_swap_slot (int pid, seL4_CPtr reply_cap, void *args) {
	printf("read_from_swap_slot\n");
	read_swap_args *read_args = (read_swap_args *) args;
	printf("1 vaddr: %p\n", (void *) read_args->vaddr);
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    alloc_args->map = KMAP;
    alloc_args->cb = read_from_swap_slot_cb;
    alloc_args->cb_args = args;
    frame_alloc_swap(pid, reply_cap, alloc_args);
	printf("read_from_swap_slot ended\n");
}

void read_from_swap_slot_cb (int pid, seL4_CPtr reply_cap, void *args) {
	printf("read_from_swap_slot_cb\n");
    frame_alloc_args *alloc_args = (frame_alloc_args *) args;
    read_swap_args *read_args = alloc_args->cb_args;
    printf("2 vaddr: %p\n", (void *) read_args->vaddr);
    int dir_index = PT_TOP(read_args->vaddr);
    int page_index = PT_BOTTOM(read_args->vaddr);
    read_args->index = alloc_args->index;
    free(alloc_args);
    assert(proc_table[pid]->page_directory[dir_index][page_index] & SWAPPED);
    read_args->slot =  proc_table[pid]->page_directory[dir_index][page_index] & SWAP_SLOT_MASK;
    proc_table[pid]->page_directory[dir_index][page_index] = read_args->index;
	
    if (!(frametable[read_args->index].frame_status & SWAP_BUFFER_MASK)) {
    	int frame_status = FRAME_IN_USE | (read_args->pid << PROCESS_BIT_SHIFT);
        frametable[buffer_tail].frame_status &= ~SWAP_BUFFER_MASK;
        frametable[buffer_tail].frame_status |= read_args->index;
        frame_status |= buffer_head;
        buffer_tail = read_args->index;
        frametable[read_args->index].frame_status = frame_status;
    } else {
        frametable[read_args->index].frame_status &= ~PROCESS_MASK;
        frametable[read_args->index].frame_status |= FRAME_IN_USE | (read_args->pid << PROCESS_BIT_SHIFT);
    }
    
    seL4_CPtr temp;
    sos_map_page_swap(read_args->index, read_args->vaddr, proc_table[pid]->vroot, 
                      proc_table[pid], pid, reply_cap, read_from_swap_slot_cb_continue,
                      read_args, &temp);
    printf("read_from_swap_slot_cb ended\n");

}

void read_from_swap_slot_cb_continue (int pid, seL4_CPtr reply_cap, void *args) {
	printf("read_from_swap_slot_cb_continue\n");
    read_swap_args *read_args = (read_swap_args *) args;
	int offset = read_args->slot * PAGE_SIZE;
	read_args->offset = offset;
	read_args->bytes_read = 0;
	printf("Offset %p, slot %d\n",(void *) offset, read_args->slot);
	int status = nfs_read(swap_handle, offset, PAGE_SIZE, swap_read_nfs_cb, (uintptr_t)args);
	// Check if RPC succeeded
	if (status != RPC_OK) {
		send_seL4_reply(reply_cap, -1);
	}
	printf("read_from_swap_slot_cb_continue ended\n");
}

void swap_read_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data) {
	printf("swap_read_nfs_cb\n");
	read_swap_args *read_args = (read_swap_args *) token;
	read_args->bytes_read += count;
	read_args->offset += count;
	assert(count != 0);
	if (status != NFS_OK) {
		printf("Exit with error: %d, %d\n", status, count);
		send_seL4_reply(read_args->reply_cap, -1);
	} else {
		printf("before memcpy, %p, %p, %d\n", (void *) read_args->vaddr, data, count);
		memcpy((void *) index_to_vaddr(read_args->index), data, count);
		printf("Read %d, total read %d, offset %p\n", count, read_args->bytes_read, (void *) read_args->offset);
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
	printf("swap_read_nfs_cb ended\n");	
}