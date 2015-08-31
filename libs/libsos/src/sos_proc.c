#include <sos.h> 

pid_t sos_process_create(const char *path) {
    printf("system call not implemented\n");
    return -1;
}
int sos_process_delete(pid_t pid) {
    printf("system call not implemented\n");
    return -1;
}

pid_t sos_my_id(void) {
    printf("system call not implemented\n");
    return -1;
}
int sos_process_status(sos_process_t *processes, unsigned max) {
    printf("system call not implemented\n");
    return -1;
}

pid_t sos_process_wait(pid_t pid) {
    printf("system call not implemented\n");
    return -1;
}
