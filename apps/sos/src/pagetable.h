#include "frametable.h"

int page_init(void);
seL4_Word sos_map_page (int ft_index, seL4_Word vaddr, seL4_ARM_PageDirectory pd);
void handle_vm_fault(seL4_Word badge, seL4_ARM_PageDirectory pd);