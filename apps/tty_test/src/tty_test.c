/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 test.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <sel4/sel4.h>


#include <sos/ttyout.h>

// Block a thread forever
// we do this by making an unimplemented system call.
static void
thread_block(void){
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, 2);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
}

int main(void){
    /* initialise communication */
    ttyout_init();
    int j = 0;
    seL4_Word *ptr = 0x5000;
    *ptr = 0x37;
    printf("Value of int 0x%08x\n", *ptr);
    do {
        for (volatile uint64_t i = 0; i < 0xFFFFFFF; i++) {}
        printf("tic %d\n", j++);
        //printf("task:\tHello world, I'm\ttty_test!\n");
		//printf("task:\tHello world, I'm\ttty_test!\n");
        // Do a direct test of sos_write
        /*char* data = (char*) malloc(sizeof(char)*10);
        int i;
        for (i = 0; i < 9; i++) {
            data[i] = i + '1';
        }
        data[9] = '\n';
        printf("SOMETHING %d\n", sos_write(data, 10));
        free(data);*/
        // Block the thread
        //thread_block();
        // sleep(1);	// Implement this as a syscall
    } while(1);

    return 0;
}
