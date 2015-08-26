#include <sos.h>

/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
int sos_getdirent(int pos, char *name, size_t nbyte) {
    assert(!"You need to implement this");
    return -1;
}

/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
int sos_stat(const char *path, sos_stat_t *buf) {
    assert(!"You need to implement this");
    return -1;
}
