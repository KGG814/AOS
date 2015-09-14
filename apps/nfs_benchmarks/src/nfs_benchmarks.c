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
#include <sos.h>

int main(void) {
    int fd = sos_sys_open("b", O_WRONLY);
    sos_sys_write(fd, "helloo", strlen("hello") + 1);
    
    return EXIT_SUCCESS;
}
