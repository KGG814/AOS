#ifndef _PROC_H_
#define _PROC_H_

#include <cspace/cspace.h>
#include <sel4/sel4.h>
#include <stdlib.h>
#include <clock/clock.h>
#include <sos/sos.h>
#include <nfs/nfs.h>

#include "callback.h"

#define CAP_TABLE_PAGES 8
#define PROCESS_MAX_FILES 16
#define MAX_PROCESSES     0xFF

#define PROC_ERR (-1)

#define PROC_READY   1
#define PROC_BLOCKED 2
#define PROC_DYING   4

#define TTY_PRIORITY         (0)

#define NO_READ 0
#define CURR_READ 1
#define CHILD_READ 2



typedef struct _child_proc child_proc;
typedef struct _wait_list wait_list;

struct _child_proc {
    int pid;
    child_proc *next;
};

struct _wait_list {
    int pid;
    wait_list *next;
};

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
    int priority;
    int parent_pid;
    int pid;
    unsigned size;
    timestamp_t create_time;
    child_proc *children;
    int reader_status;
    seL4_CPtr wait_cap;

    //number of processes this process is waiting for to die
    int delete_wait;

    char command[N_NAME];
} addr_space; 

typedef struct _start_process_args {
    char *app_name;
    seL4_CPtr fault_ep;
    int priority;
    callback_ptr cb;
    void *cb_args;
    int parent_pid;
    // not initialised
    char* elf_base;

    // #JustElfLoadThings
    seL4_CPtr reply_cap;
    fattr_t *elf_attr;
    fhandle_t *elf_fh;
    int curr_elf_frame;
    int max_elf_frames;
    int *elf_load_table;
    seL4_Word curr_elf_addr;
    int offset;
    int child_pid;
} start_process_args;

typedef struct _new_as_args {
    callback_ptr cb;
    void *cb_args;  
    // Not initalised
    int new_pid;
    int parent_pid;
} new_as_args;

addr_space* proc_table[MAX_PROCESSES + 1];

//this is the endpoint all processes will be attached to
seL4_CPtr _sos_ipc_ep_cap;

void proc_table_init(void);

void new_as(int pid, seL4_CPtr reply_cap, void *args);
void cleanup_as(int pid);

void start_process(int pid, seL4_CPtr reply_cap, void *args);

void start_process_load (int parent_pid, seL4_CPtr reply_cap, void *_args);

void process_status(seL4_CPtr reply_cap
                   ,int pid
                   ,sos_process_t* processes
                   ,unsigned max_processes
                   );

void handle_process_create_cb (int pid, seL4_CPtr reply_cap, void *args, int err);

int is_child(int parent_pid, int child_pid);

void remove_child(int parent_pid, int child_pid);

void kill_process(int delete_pid, int child_pid, seL4_CPtr reply_cap);

void kill_process_cb(int pid, seL4_CPtr reply_cap, void *data, int err);

void add_to_wait_list(int pid);

void wake_wait_list(int pid);
#endif /* _PROC_H_ */
