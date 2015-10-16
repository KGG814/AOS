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
#include <sos/sos.h>

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

size_t sos_write(const void *vData, size_t count) {
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

int sos_sys_open(const char *path, fmode_t mode) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, OPEN);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_SetMR(2, mode);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_sys_close(int file) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, CLOSE);
    seL4_SetMR(1, file);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_sys_read(int file, char *buf, size_t nbyte) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, READ);
    seL4_SetMR(1, file);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_SetMR(3, nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_sys_write(int file, const char *buf, size_t nbyte) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, WRITE);
    seL4_SetMR(1, file);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_SetMR(3, nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_getdirent(int pos, char *name, size_t nbyte) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, GETDIRENT);
    seL4_SetMR(1, pos);
    seL4_SetMR(2, (seL4_Word)name);
    seL4_SetMR(3, nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_stat(const char *path, sos_stat_t *buf) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, STAT);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
    
}

pid_t sos_process_create(const char *path) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, P_CREATE);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}
int sos_process_delete(pid_t pid) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, P_DELETE);
    seL4_SetMR(1, (seL4_Word)pid);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

pid_t sos_my_id(void) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, MY_ID);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_process_status(sos_process_t *processes, unsigned max) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, P_STATUS);
    seL4_SetMR(1, (seL4_Word) processes);
    seL4_SetMR(2, (seL4_Word) max);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

pid_t sos_process_wait(pid_t pid) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, P_WAIT);
    seL4_SetMR(1, (seL4_Word)pid);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

void sos_sys_exit(void) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, P_EXIT);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
}
