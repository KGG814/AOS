#include <sos.h> 

/* Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
pid_t sos_process_create(const char *path) {
    assert(!"You need to implement this");
    return -1;
}

/* Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
int sos_process_delete(pid_t pid) {
    assert(!"You need to implement this");
    return -1;
}

/* Returns ID of caller's process. */
pid_t sos_my_id(void) {
    assert(!"You need to implement this");
    return -1;
}

/* Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
int sos_process_status(sos_process_t *processes, unsigned max) {
    assert(!"You need to implement this");
    return -1;
}

/* Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
pid_t sos_process_wait(pid_t pid) {
    assert(!"You need to implement this");
    return -1;
}
