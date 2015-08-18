#include "ft_tests.h"
#include <sys/debug.h>

#define verbose 5

int basic_test(void) { 
    for (int i = 0; i < 10; i++) {
        seL4_Word vaddr;
        frame_alloc(&vaddr);
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
        seL4_Word vaddr;
        frame_alloc(&vaddr);
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
    seL4_Word vaddr = 0;
    seL4_Word prev = 0;
    int err = 0; 
    for (int i = 0;; i++) {
<<<<<<< HEAD
         prev = vaddr;
         vaddr = frame_alloc();
         assert(vaddr);
         *((seL4_Word *) vaddr) = 0x37;
         assert(*((seL4_Word *) vaddr) == 0x37);

         if (!(i % 10000))dprintf(0, "Page #%d allocated at %p\n",  i, vaddr);

         err = frame_free(vaddr);
         if (err) {
            dprintf(0, "something went wrong.\n");
         }
=======
        seL4_Word vaddr;
        frame_alloc(&vaddr);
        assert(vaddr);
        *((seL4_Word *) vaddr) = 0x37;
        assert(*((seL4_Word *) vaddr) == 0x37);

        dprintf(0, "Page #%d allocated at %p\n",  i, vaddr);

        frame_free(vaddr);
>>>>>>> 5195f9cdff37d27a0c1bcacdb115d4ac937303e8
    }
    return PASSED;
}

int all_tests(void) {
    return (basic_test() == PASSED && oom_test() == PASSED) ? PASSED : FAIL;
}
