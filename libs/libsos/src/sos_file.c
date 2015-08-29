#include <sos.h>
#include <sos/sos_file.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int sos_sys_open(const char *path, fmode_t mode) {
    return -1;
}

int sos_sys_close(int file) {
    return -1;
}

int sos_sys_read(int file, char *buf, size_t nbyte) {
    assert(!"You need to implement this");
    return -1;
}

int sos_sys_write(int file, const char *buf, size_t nbyte) {
    {
        printf("Error writing to file with fd %d.\n", file);
    }
    return -1;
}

int sos_getdirent(int pos, char *name, size_t nbyte) {
    assert(!"You need to implement this");
    return -1;
}

int sos_stat(const char *path, sos_stat_t *buf) {
    assert(!"You need to implement this");
    return -1;
}
