#ifndef _PROC_H_
#define _PROC_H_

#include <cspace/cspace.h>
#include <sel4/sel4.h>
#include <stdlib.h>
#include <clock/clock.h>
#include <sos/sos.h>

#define CAP_TABLE_PAGES 8
#define PROCESS_MAX_FILES 16
#define MAX_PROCESSES     0xFF

#define PROC_ERR (-1)

#define WAITING (1 << 0)
#define KILLED  (1 << 1)

#define TTY_PRIORITY         (0)

typedef void (*callback_ptr)(int, seL4_CPtr, void*);

typedef struct _addr_space addr_space; 

typedef struct _child_proc child_proc;

struct _child_proc {
    int pid;
    child_proc *next;
};

struct _addr_space {
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
    int priority;
    int parent_pid;
    int pid;
    unsigned size;
    timestamp_t create_time;
    child_proc *children;
    char command[N_NAME];
}; 

typedef struct _start_process_args {
    char *app_name;
    seL4_CPtr fault_ep;
    int priority;
    callback_ptr cb;
    void *cb_args;
    int parent_pid;
    // not initialised
    char* elf_base;
} start_process_args;

typedef struct _new_as_args {
    callback_ptr cb;
    void *cb_args;  
    // Not initalised
    int new_pid;
} new_as_args;

addr_space* proc_table[MAX_PROCESSES + 1];

//this is the endpoint all processes will be attached to
seL4_CPtr _sos_ipc_ep_cap;

void proc_table_init(void);

void new_as(int pid, seL4_CPtr reply_cap, void *args);
void cleanup_as(int pid);

void start_process(int pid, seL4_CPtr reply_cap, void *args);

void process_status(seL4_CPtr reply_cap
                   ,int pid
                   ,sos_process_t* processes
                   ,unsigned max_processes
                   );

void handle_process_create_cb (int pid, seL4_CPtr reply_cap, void *args);
#endif /* _PROC_H_ */
