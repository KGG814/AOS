#include <sel4/types.h>


int swap_init(void);
void write_to_swap_slot(int pid, seL4_CPtr reply_cap, void *args);