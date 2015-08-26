#include <sos.h>
#include <sos/sos_file.h>
#include <string.h>
#include <stdio.h>

//TODO move this into the process struct. for now this is for just the single 
//user process
sos_stat_t files[PROCESS_MAX_FILES]; /* file descriptor table */
int next_free_fd[PROCESS_MAX_FILES]; /* for finding free frames quickly */

//status flag for console to see if it is currently open for reading
int con_read = 0;

int cur_free = 0;

int fdt_init(void) {
    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        files[i] = NULL;
        next_free_fd[i] = i + 1;
    }
}

int sos_sys_open(const char *path, fmode_t mode) {
    //for console
    if (cur_free = PROCESS_MAX_FILES) {
        //filetable full
        return -1; 
    }

    int fd = cur_free;
    cur_free = next_free_fd[cur_free];
    next_free_fd[fd] = -1; //currently occupied

    if (strncmp("console", path, strlen("console")) == 0) {
        if (mode & FM_READ) {
            if (con_read) {
                //console is already open for reading 
                return -1;
            }     
            con_read = 1;
            files[fd].st_type = ST_SPECIAL;
            files[fd].st_fmode = mode & 0x7; 
        } 
        return fd;
    } else {
        printf("Error opening file: %s\n", path);
    }
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
