#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sel4/sel4.h>

#define NPAGES 27

/* called from pt_test */
static void do_pt_test( char *buf ) {
    int i;
    printf("doing test\n");
    /* set */
    for(i = 0; i < NPAGES; i ++) {
        buf[i * 4096] = i;
    }
	

    /* check */
    for(i = 0; i < NPAGES; i ++)
	assert(buf[i * 4096] == i);
}

int main( void ) {
    printf("Starting vm test\n");
    int *x = malloc(sizeof(int));
    *x = 9;
    printf("TEST1 %d\n", *x);
    // heap test 
    char * buf1 = malloc(NPAGES * 4096);
    printf("Buffer 1 allocated\n");
    assert(buf1);
    do_pt_test(buf1);
    char * buf2 = malloc(NPAGES * 4096);
    assert(buf2);
    do_pt_test(buf2);
    char * buf3 = malloc(NPAGES * 4096);
    assert(buf3);
    do_pt_test(buf3);
    char * buf4 = malloc(NPAGES * 4096);
    assert(buf4);
    do_pt_test(buf4);
    char * buf5 = malloc(NPAGES * 4096);
    assert(buf5);
    do_pt_test(buf5);

    free(buf1);
    free(buf2);
    free(buf3);
    free(buf4);
    free(buf5);

    // need a decent sized stack
    char buf6[NPAGES * 4096];
    
    // check the stack is above phys mem
    assert((void *) buf1 > (void *) 0x20000000);

    // stack test 
    do_pt_test(buf1);

    
    printf("vm test passed\n");
    return 0;
}
