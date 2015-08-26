#include <sos.h> 

pid_t sos_process_create(const char *path) {
    assert(!"You need to implement this");
    return -1;
}
int sos_process_delete(pid_t pid) {
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_my_id(void) {
    assert(!"You need to implement this");
    return -1;
}
int sos_process_status(sos_process_t *processes, unsigned max) {
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_process_wait(pid_t pid) {
    assert(!"You need to implement this");
    return -1;
}
