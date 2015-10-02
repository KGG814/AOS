#include "proc.h"

void proc_table_init(void) {
    memset(proc_table, 0, MAX_PROCESSES * sizeof(addr_space*));
}
