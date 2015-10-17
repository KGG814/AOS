#ifndef _SWAP_H_
#define _SWAP_H_

#include "callback.h"

typedef struct _write_swap_args {
	callback_ptr cb;
	void *cb_args;
	int index;
	int pid;
	seL4_CPtr reply_cap;
	// Not initialised
	int slot;
	int offset;
	int bytes_written;
	seL4_Word addr;
} write_swap_args;

typedef struct _read_swap_args {
	callback_ptr cb;
	void *cb_args;
	seL4_Word vaddr;
	int pid;
	seL4_CPtr reply_cap;
	// Not initialised
	int slot;
	int index;
	int offset;
	int bytes_read;
} read_swap_args;

int swap_init(void);
void write_to_swap_slot(int pid, seL4_CPtr reply_cap, write_swap_args *args);
void read_from_swap_slot (int pid, seL4_CPtr reply_cap, read_swap_args *args);
void free_swap_slot(int slot);

#endif /* _SWAP_H_ */
