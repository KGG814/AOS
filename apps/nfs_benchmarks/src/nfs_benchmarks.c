#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <utils/time.h>

int main(void) {
    int fd = sos_sys_open("console", O_WRONLY);
    sos_sys_write(fd, "test\n", 5);

    return EXIT_SUCCESS;
}
