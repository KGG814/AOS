#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sel4/sel4.h>
#include <time.h>
#include <sos/sos.h>
#define NPAGES 27


/* called from pt_test */
static void do_pt_test( char *buf ) {

    /* set */
    for(int i = 0; i < NPAGES; i ++) {
        buf[i * 4096] = i;
    }
	

    /* check */
    for(int i = 0; i < NPAGES; i ++) {
        assert(buf[i * 4096] == i);
    }
	
}

int main( void ) {
    printf("%ld\n", (long)sos_sys_time_stamp());
    printf("%ld\n", (long)sos_sys_time_stamp());
    printf("%ld\n", (long)sos_sys_time_stamp());
    printf("%ld\n", (long)sos_sys_time_stamp());
    /* need a decent sized stack */
    printf("Getting stack\n");
    char buf1[NPAGES * 4096], *buf2 = NULL;
    printf("Stack acquired\n");
    /* check the stack is above phys mem */
    //assert((void *) buf1 > (void *) 0x20000000);

    /* stack test */
    printf("Doing stack test\n");
    do_pt_test(buf1);
    printf("Stack test success\n");
    /* heap test */
    
    printf("Doing malloc\n");
    buf2 = malloc(NPAGES * 4096);
    assert(buf2);
    printf("Malloc success\n");
    printf("Doing heap test\n");
    do_pt_test(buf2);
    free(buf2);
    printf("Heap test success\n");
    printf("Starting sleep test\n");
    sos_sys_usleep(5000);
    printf("Sleep test success\n");

    return 0;
}
