#ifndef _SWAP_H_
#define _SWAP_H_

#include <sel4/types.h>

int swap_init(void);
void write_to_swap_slot(int pid, seL4_CPtr reply_cap, void *args);

#endif /* _SWAP_H_ */