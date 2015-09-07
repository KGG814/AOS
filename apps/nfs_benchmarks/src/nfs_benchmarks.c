#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sel4/sel4.h>
#include <time.h>
#include <sos.h>

int main(void) {
    int fd = sos_sys_open("console", O_WRONLY);
    sos_sys_write(fd, "test\n", 5);

    return EXIT_SUCCESS;
}
