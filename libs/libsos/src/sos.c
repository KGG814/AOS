/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sos.h>

//needed for string comparisons
#include <string.h>

#include <math.h>

#include <sel4/sel4.h>
#include <clock/clock.h>

/*
 * Masks for getting 1, 2 or 3 characters form the last message
 */
#define CHAR_MASK_1 0x000000FF
#define CHAR_MASK_2 0x0000FFFF
#define CHAR_MASK_3 0x00FFFFFF
#define min(a, b) (((a) < (b)) ? (a) : (b)) 



static size_t sos_debug_print(const void *vData, size_t count) {
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++)
        seL4_DebugPutChar(realdata[i]);
    return count;
}

size_t sos_write(void *vData, size_t count) {
    seL4_Word msg;
    for (int sentMessages = 0; sizeof(seL4_Word)*sentMessages < count; sentMessages += (seL4_MsgMaxLength-1)) {
        //Number of messages required
        int length = ceil((count-sizeof(seL4_Word)*sentMessages)/(double)sizeof(seL4_Word));
    	if (length > seL4_MsgMaxLength-1) {
    	   length = seL4_MsgMaxLength-1;
    	}
        // Need messages for the data and one for the syscall number
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length+1);
        seL4_SetTag(tag);    
		      
        seL4_SetMR(SYSCALL, SOS_WRITE);
        // Put every 4 bytes into a message
        seL4_Word *wordPtr = (seL4_Word*) vData + sentMessages;
        for (int message = 1; message < length; message++) {
            seL4_SetMR(message, *wordPtr);
            wordPtr++; 
        }
        // The number of characters in the last message
        int tailChars = count % sizeof(seL4_Word);
        // Get the last message so we can edit out junk data
        msg = *wordPtr;
        switch (tailChars) {
        case 1:
            msg &= CHAR_MASK_1;
            break;
        case 2:
            msg &= CHAR_MASK_2;
            break;
        case 3:
            msg &= CHAR_MASK_3;
            break;
        }
        seL4_SetMR(length, msg); 
        seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    }
    return count;
}

size_t sos_read(void *vData, size_t count) {
    //implement this to use your syscall
    return 0;
}

void sos_sys_usleep(int msec) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, USLEEP);
    seL4_SetMR(1, msec);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
}

int64_t sos_sys_time_stamp(void) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, TIMESTAMP);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    int64_t timestamp = seL4_GetMR(0);
    timestamp = timestamp << 32;
    timestamp += seL4_GetMR(1);
    return timestamp;
}


