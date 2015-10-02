#ifndef _PROC_H_
#define _PROC_H_

#include <cspace/cspace.h>
#include <sos.h>
#include <clock/clock.h>

#define CAP_TABLE_PAGES 4
#define PROCESS_MAX_FILES 16
#define MAX_PROCESSES     0xFF

#define PROC_ERR (-1)

#define WAITING (1 << 0)
#define KILLED  (1 << 1)

typedef struct _addr_space {
	seL4_Word vroot_addr;
	seL4_ARM_PageDirectory vroot;

    seL4_Word** page_directory;
    seL4_ARM_PageTable* cap_table[CAP_TABLE_PAGES];

    seL4_Word ipc_buffer_addr;
    seL4_CPtr ipc_buffer_cap;

    seL4_Word tcb_addr;
    seL4_TCB tcb_cap;

    cspace_t *croot;
    seL4_Word brk;
    int file_table[PROCESS_MAX_FILES];
    int n_files_open;

    /* process properties */
    int status;
    pid_t pid;
    unsigned size;
    timestamp_t create_time;
    char command[N_NAME];

} addr_space; 

addr_space* proc_table[MAX_PROCESSES + 1];

//this is the endpoint all processes will be attached to
seL4_CPtr _sos_ipc_ep_cap;

void proc_table_init(void);

int new_as();
void cleanup_as(int pid);

int start_process(char* app_name, seL4_CPtr fault_ep, int priority);

void process_status(seL4_CPtr reply_cap
                   ,int pid
                   ,sos_stat_t* processes
                   ,unsigned max_processes
                   );

#endif /* _PROC_H_ */
