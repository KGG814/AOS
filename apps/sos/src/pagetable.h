#include "frametable.h"
#include "proc.h"
#include <sel4/sel4.h>

#define PAGE_MASK   0xFFFFF000

#define GUARD_PAGE_FAULT 	1
#define UNKNOWN_REGION		2
#define NULL_DEREF			3

#define EFAULT				4

int page_init(int pid);
seL4_CPtr sos_map_page (int ft_index, seL4_Word vaddr, seL4_ARM_PageDirectory pd, addr_space* as);
void handle_vm_fault(seL4_Word badge, int pid);
seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, int pid);
void user_buffer_map(seL4_Word user_ptr, size_t nbyte, int pid);
int map_if_valid(seL4_Word vaddr, int pid);
int check_region(seL4_Word start, seL4_Word end);