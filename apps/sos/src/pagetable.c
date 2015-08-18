#include <sel4/types.h>
#include "ut_manager/ut.h"
#include <cspace/cspace.h>
#include <mapping.h>
#include <sys/panic.h>
#include <stdlib.h>
#include "frametable.h"

seL4_Word** page_directory = (seL4_Word**)0x30000000;
seL4_Word*  page_tables    = (seL4_Word*) 0x30004000;
seL4_CPtr*  cap_directory  = (seL4_CPtr*) 0x40000000;
seL4_CPtr   page_directory_cap;
seL4_CPtr   cap_directory_cap;

#define PAGEDIR_SIZE   4096
#define PAGE_SIZE   4096
#define BOTTOM(x)  ((x) & 0x3FF)
#define TOP(x)  (((x) & 0xFFC00) >> 10)

int page_init(void) {
    page_directory = (seL4_Word**)malloc(PAGEDIR_SIZE*sizeof(char));

}

seL4_Word sos_map_page (int ft_index, seL4_Word vaddr) {
	seL4_Word dir_index = TOP(vaddr);
	seL4_Word page_index = BOTTOM(vaddr);

    // THIS
	/* Check that the page table exists */
	if (page_directory[dir_index] == NULL) {

        page_directory[dir_index] = (seL4_Word*)malloc(PAGEDIR_SIZE*sizeof(char));
	}
	/* Map into the page table */
	page_directory[dir_index][page_index] = ft_index;
    return 0;
}