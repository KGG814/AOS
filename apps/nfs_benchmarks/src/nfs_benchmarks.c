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


void write_test(void) {
    char buf[4096 * 16] = {};
    char out[256] = {};
    char out_name[256] = {};
    for (int i = 0; i < 4096*16/128; i++) {
        strcpy(buf + 16*i
              ,
"\
XXX_this string should be exactly 128 characters long, starting from initial XXX_ and then ending at the next _XXX, ie this _XXX" 

              ); 
    }

    buf[4096 * 16] = '\0';

    
    int write_bm_out = sos_sys_open("write_bm", O_WRONLY);
    sprintf(out, "buflen start end\n");
    sos_sys_write(write_bm_out, out, strlen(out));
    
    for (int i = 16; i <= 4096; i = i * 2) {
        printf("doing write test with buflen %d\n", i);
        sprintf(out_name, "test_%d", i);
        int fd = sos_sys_open(out_name, O_WRONLY);
        int n_loops = 4096 * 16 / i;
        uint64_t start = sos_sys_time_stamp(); 
        for (int j = 0; j < n_loops; j++) {
            sos_sys_write(fd, buf + i*j, i);
        }
        uint64_t end = sos_sys_time_stamp();
        sprintf(out, "%d, %llu, %llu\n", i, start, end);
        sos_sys_write(write_bm_out, out, strlen(out));
        sos_sys_close(fd);
    }
}

void read_test(void) {
    char buf[4096 * 8] = {};
    char out[256] = {};
    char out_name[256] = {};

    int read_bm_out = sos_sys_open("read_bm", O_WRONLY);
    sprintf(out, "buflen start end\n");
    sos_sys_write(read_bm_out, out, strlen(out));
    
    for (int i = 16; i <= 4096; i = i * 2) {
        printf("doing read test with buflen %d\n", i);
        sprintf(out_name, "test_%d", i);
        int fd = sos_sys_open(out_name, O_WRONLY);
        int n_loops = 4096 * 8 / i;
        uint64_t start = sos_sys_time_stamp(); 
        for (int j = 0; j < n_loops; j++) {
            sos_sys_read(fd, buf + i*j, i);
        }
        uint64_t end = sos_sys_time_stamp();
        sprintf(out, "%d, %llu, %llu\n", i, start, end);
        sos_sys_write(read_bm_out, out, strlen(out));
        sos_sys_close(fd);
    }
}

int main() {
    write_test();
    read_test();
}
