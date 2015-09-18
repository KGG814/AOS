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

#define BUF_SIZ (4096 * 20)
#define N_SIZES (15) 

int buf_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 10240, 16384, 20480, 40960, 81920};

void write_test(void) {
    char buf[BUF_SIZ] = {};
    char out[256] = {};
    char out_name[256] = {};
    for (int i = 0; i < BUF_SIZ/128; i++) {
        strcpy(buf + 128*i
              ,
"\
XXX_this string should be exactly 128 characters long, starting from initial XXX_ and then ending at the next _XXX, ie this _XXX" 

              ); 
    }

    //buflen, t_start, t_end
    int write_bm_out = sos_sys_open("write_bm", O_WRONLY);
    
    for (int i = 5; i < N_SIZES; i++) {
        int sz = buf_sizes[i];
        printf("doing write test with buflen %d\n", sz);
        sprintf(out_name, "test_%d", sz);
        int fd = sos_sys_open(out_name, O_WRONLY);
        int n_loops = BUF_SIZ / sz;
        uint64_t start = sos_sys_time_stamp(); 
        for (int j = 0; j < n_loops; j++) {
            printf("currently writing to %p\n", buf + sz*j);
            int bytes = sos_sys_write(fd, buf + sz*j, sz);
            assert(bytes);
        }
        uint64_t end = sos_sys_time_stamp();
        sprintf(out, "%d, %llu, %llu\n", sz, start, end);
        sos_sys_write(write_bm_out, out, strlen(out));
        sos_sys_close(fd);
    }
}

void read_test(void) {
    char buf[BUF_SIZ] = {};
    char out[256] = {};
    char out_name[256] = {};

    int read_bm_out = sos_sys_open("read_bm", O_WRONLY);
    
    for (int i = 5; i < N_SIZES; i++) {
        int sz = buf_sizes[i];
        int read = 0;
        int zero_count = 0;
        printf("doing read test with buflen %d\n", sz);
        sprintf(out_name, "test_%d", sz);
        int fd = sos_sys_open(out_name, O_RDONLY);
        int n_loops = BUF_SIZ/sz;
        uint64_t start = sos_sys_time_stamp(); 
        for (int j = 0; j < n_loops; j++) {
            read += sos_sys_read(fd, buf + sz*j, sz);
            if (read ==0) {
                zero_count++;
            }
        }
        printf("Wanted to read %d, actually read %d, read zero %d tiems\n", BUF_SIZ, read, zero_count);
        assert(read == BUF_SIZ);
        uint64_t end = sos_sys_time_stamp();
        sprintf(out, "%d, %llu, %llu\n", i, start, end);
        sos_sys_write(read_bm_out, out, strlen(out));
        sos_sys_close(fd);
    }
}

int main() {
    //write_test();
    read_test();
}
