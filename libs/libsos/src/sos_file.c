#include <sos.h>
#include <sos/sos_file.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int sos_sys_open(const char *path, fmode_t mode) {
    // 9242_TODO Error checking
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, OPEN);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_SetMR(2, mode);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_sys_close(int file) {
    // 9242_TODO Error checking
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, CLOSE);
    seL4_SetMR(1, file);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
}

int sos_sys_read(int file, char *buf, size_t nbyte) {
    // 9242_TODO Error checking
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
    // 9242_TODO Error checking
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
    printf("syscall not implemented\n");
    return -1;
    // 9242_TODO Error checking
    /*
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, GETDIRENT);
    seL4_SetMR(1, pos);
    seL4_SetMR(2, (seL4_Word)name);
    seL4_SetMR(3, nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
    */
}

int sos_stat(const char *path, sos_stat_t *buf) {
    /*printf("syscall not implemented\n");
    return -1;*/
    // 9242_TODO Error checking

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(SYSCALL, STAT);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    return seL4_GetMR(0);
    
}
