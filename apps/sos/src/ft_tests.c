#include "ft_tests.h"
#include <sys/debug.h>

#define verbose 5

int basic_test(void) { 
    for (int i = 0; i < 10; i++) {
        seL4_Word vaddr = frame_alloc();
        assert(vaddr);

        *((seL4_Word *) vaddr) = 0x37;
        assert(*((seL4_Word *) vaddr) == 0x37);

        dprintf(0, "Page #%d allocated at %p\n",  i, (void *) vaddr);
    }
    return PASSED;
}

int oom_test(void) {
    seL4_Word i = 0;
    for (;;) {
        i++;
         seL4_Word vaddr = frame_alloc();
         if (!vaddr) {
    	  dprintf(0, "Out of memory!\n");
    	  break;
         }

         if (!(i % 1000)) dprintf(0, "Page #%d allocated at %p\n", i , (void *) vaddr);
         *((seL4_Word *) vaddr) = 0x37;
         assert(*((seL4_Word *) vaddr) == 0x37);
    }
    return PASSED;
}

int free_test(void) {
    for (int i = 0;; i++) {
         seL4_Word vaddr = frame_alloc();
         assert(vaddr);
         *((seL4_Word *) vaddr) = 0x37;
         assert(*((seL4_Word *) vaddr) == 0x37);

         dprintf(0, "Page #%d allocated at %p\n",  i, vaddr);

         frame_free(vaddr);
    }
    return PASSED;
}

int all_tests(void) {
    return (basic_test() == PASSED && oom_test() == PASSED) ? PASSED : FAIL;
}