#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sel4/sel4.h>

#define NPAGES 27

/* called from pt_test */
static void do_pt_test( char *buf ) {
    int i;

    /* set */
    for(i = 0; i < NPAGES; i ++)
	buf[i * 4096] = i;

    /* check */
    for(i = 0; i < NPAGES; i ++)
	assert(buf[i * 4096] == i);
}

int main( void ) {
    /* need a decent sized stack */
    char buf1[NPAGES * 4096], *buf2 = NULL;

    /* check the stack is above phys mem */
    assert((void *) buf1 > (void *) 0x20000000);

    /* stack test */
    do_pt_test(buf1);

    /* heap test */
    buf2 = malloc(NPAGES * 4096);
    assert(buf2);
    do_pt_test(buf2);
    free(buf2);
    return 0;
}
