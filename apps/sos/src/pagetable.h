#include "frametable.h"
#include "proc.h"
#include <sel4/sel4.h>

int page_init(addr_space* as);
seL4_CPtr sos_map_page (int ft_index, seL4_Word vaddr, seL4_ARM_PageDirectory pd, addr_space* as);
void handle_vm_fault(seL4_Word badge, seL4_ARM_PageDirectory pd, addr_space* as);
seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, addr_space* as);