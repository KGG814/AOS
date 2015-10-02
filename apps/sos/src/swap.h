#include <sel4/types.h>

typedef void (*callback_ptr)(int, seL4_CPtr, void*);

typedef struct _write_swap_args {
	callback_ptr cb;
	void *cb_args;
	int index;
	int pid;
	seL4_CPtr reply_cap;
	// Not initialised
	int slot;
} write_swap_args;

typedef struct _read_swap_args {
	callback_ptr cb;
	void *cb_args;
	seL4_Word vaddr;
	int pid;
	seL4_CPtr reply_cap;
	// Not initialised
	int index;
} read_swap_args;

int swap_init(void);
void write_to_swap_slot(int pid, seL4_CPtr reply_cap, void *args);
void read_from_swap_slot (int pid, seL4_CPtr reply_cap, void *args);