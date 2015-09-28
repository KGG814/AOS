#include "frametable.h"
#include "proc.h"
#include <sel4/sel4.h>

#define PAGE_MASK   		0xFFFFF000
#define SWAPPED     		0x80000000
#define SWAP_SLOT_MASK      0x7FFFFFFF
#define GUARD_PAGE_FAULT 	1
#define UNKNOWN_REGION		2
#define NULL_DEREF			3

#define EFAULT				4

#define PT_BOTTOM(x)        (((x) & 0x3FF000) >> 12)
#define PT_TOP(x)           (((x) & 0xFFC00000) >> 22)

int page_init(int pid);
typedef void (*callback_ptr)(int, seL4_CPtr, void*);

seL4_CPtr sos_map_page(int ft_index
                      ,seL4_Word vaddr
                      ,seL4_ARM_PageDirectory pd
                      ,addr_space* as
                      ,int pid
                      );
void handle_vm_fault(seL4_Word badge, int pid);
seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, int pid);
void user_buffer_map(seL4_Word user_ptr, size_t nbyte, int pid);
int map_if_valid(seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap);
int check_region(seL4_Word start, seL4_Word end);
int copy_page (seL4_Word dst
              ,int count
              ,seL4_Word src
              ,int pid
              );
void handle_vm_fault_cb(int pid, seL4_CPtr cap, void* args);