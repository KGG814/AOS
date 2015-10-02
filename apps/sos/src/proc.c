#include <string.h>

#include "proc.h"
#include "pagetable.h"
#include "file_table.h"

void proc_table_init(void) {
    memset(proc_table, 0, (MAX_PROCESSES + 1) * sizeof(addr_space*));
}

int new_as() {
    int pid = 1;
    while (pid <= MAX_PROCESSES && proc_table[pid] != NULL) {
        pid++;
    }

    if (pid > MAX_PROCESSES) {
        return PROC_ERR;
    }

    addr_space *as = malloc(sizeof(addr_space)); 
    if (as == NULL) {
        //really nothing to be done
        return PROC_ERR;
    }
    
    proc_table[pid] = as;
    
    int err = fdt_init(pid);
    if (err) {
        free(as);
        proc_table[pid] = NULL;
        return PROC_ERR;
    }

    err = page_init(pid);
    if (err) {
        fdt_cleanup(pid);
        free(as);
        proc_table[pid] = NULL;
    }

    return pid;
}

void cleanup_as(int pid) {
    if (pid < 1 || pid > MAX_PROCESSES) {
        //invalid pid
        return;
    }

    addr_space *as = proc_table[pid];
    if (as == NULL) {
        //nonexistent as 
        return;
    }

    //clean shit up here
    fdt_cleanup(pid);
    pt_cleanup(pid);

    free(as);
    proc_table[pid] = NULL;
} 
