#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cpio/cpio.h>
#include <elf/elf.h>

#include "ut_manager/ut.h"
#include <sos/vmem_layout.h>
#include <sos/sos.h>

#include <clock/clock.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#include "syscalls.h"
#include "network.h"
#include "elf.h"
#include "ft_tests.h"
#include "mapping.h"
#include "file_table.h"
#include "vfs.h"
#include "proc.h"
#include "pagetable.h"
#include "file_table.h"

extern char _cpio_archive[];

addr_space* proc_table[MAX_PROCESSES + 1];
int num_processes = 0;
int next_pid = MAX_PROCESSES;

void process_status_cb(int pid, seL4_CPtr reply_cap, void *_args);

void proc_table_init(void) {
    memset(proc_table, 0, (MAX_PROCESSES + 1) * sizeof(addr_space*));
}

int new_as() {
    if (num_processes == MAX_PROCESSES) {
        return PROC_ERR;
    }

    int pid = next_pid;
    for (int i = 0; i < MAX_PROCESSES && proc_table[next_pid] != NULL; i++) {
        next_pid = (next_pid % MAX_PROCESSES) + 1;
    }

    pid = next_pid;

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

    as->status = 0;

    as->pid = pid;
    as->size = 0;
    as->create_time = time_stamp();
    
    next_pid = (next_pid % MAX_PROCESSES) + 1;
    num_processes++;
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
    num_processes--;

    free(as);
    proc_table[pid] = NULL;
} 

int start_process(char *app_name, seL4_CPtr fault_ep, int priority) {
    int pid = new_as(app_name);
    int err;
    seL4_CPtr user_ep_cap;
    seL4_Word temp;
    int index;
    addr_space* as = proc_table[pid];
    /* These required for setting up the TCB */
    seL4_UserContext context;

    /* These required for loading program sections */
    char* elf_base;
    unsigned long elf_size;

    /* Create a VSpace */
    as->vroot_addr = ut_alloc(seL4_PageDirBits);
    conditional_panic(!as->vroot_addr, 
                      "No memory for new Page Directory");
    err = cspace_ut_retype_addr(as->vroot_addr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &(as->vroot));
    conditional_panic(err, "Failed to allocate page directory cap for client");

    /* Create a simple 1 level CSpace */
    as->croot = cspace_create(1);
    assert(as->croot != NULL);   
    /* Create an IPC buffer */
    index = frame_alloc(&temp, NOMAP, pid);
    as->ipc_buffer_addr = index_to_paddr(index);
    as->ipc_buffer_cap = frametable[index].frame_cap;
    /* Map IPC buffer*/
    err = map_page_user(as->ipc_buffer_cap, as->vroot,
                   PROCESS_IPC_BUFFER,
                   seL4_AllRights, seL4_ARM_Default_VMAttributes, as);
    frametable[index].frame_status |= FRAME_DONT_SWAP;
    conditional_panic(err, "Unable to map IPC buffer for user app");
    /* Copy the fault endpoint to the user app to enable IPC */
    user_ep_cap = cspace_mint_cap(as->croot,
                                  cur_cspace,
                                  fault_ep,
                                  seL4_AllRights, 
                                  seL4_CapData_Badge_new(pid));
    
    //???
    /* should be the first slot in the space, hack I know */
    //assert(user_ep_cap == 1);
    //assert(user_ep_cap == USER_EP_CAP);
    
    /* Create a new TCB object */
    as->tcb_addr = ut_alloc(seL4_TCBBits);
    conditional_panic(!as->tcb_addr, "No memory for new TCB");
    err =  cspace_ut_retype_addr(as->tcb_addr,
                                 seL4_TCBObject,
                                 seL4_TCBBits,
                                 cur_cspace,
                                 &(as->tcb_cap));
    conditional_panic(err, "Failed to create TCB");

    /* Configure the TCB */
    err = seL4_TCB_Configure(as->tcb_cap, user_ep_cap, priority,
                             as->croot->root_cnode, seL4_NilData,
                             as->vroot, seL4_NilData, PROCESS_IPC_BUFFER,
                             as->ipc_buffer_cap);
    conditional_panic(err, "Unable to configure new TCB");


    /* parse the cpio image */
    dprintf(1, "\nStarting \"%s\"...\n", app_name);
    elf_base = cpio_get_file(_cpio_archive, app_name, &elf_size);
    conditional_panic(!elf_base, "Unable to locate cpio header");
    /* load the elf image */
    err = elf_load(as->vroot, elf_base, as);
    conditional_panic(err, "Failed to load elf image");
    
    memset(as->command, 0, N_NAME);
    strncpy(as->command, app_name, N_NAME - 1); 

    /* Start the new process */
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(elf_base);
    context.sp = PROCESS_STACK_TOP;
    seL4_TCB_WriteRegisters(as->tcb_cap, 1, 0, 2, &context);
    return pid;
}

void process_status(seL4_CPtr reply_cap
                   ,int pid
                   ,sos_process_t* processes
                   ,unsigned max_processes
                   ) {
    if (processes == NULL) {
        send_seL4_reply(reply_cap, 0);
    }
    
    sos_process_t* k_ptr = malloc(sizeof(sos_process_t) * max_processes);
    if (k_ptr == NULL) {
        send_seL4_reply(reply_cap, 0);
    }
    
    copy_out_args *cpa = malloc(sizeof(copy_out_args));
    if (cpa == NULL) {
        send_seL4_reply(reply_cap, 0); 
    }

    int count = 0;
    
    for (int i = 1; count < num_processes && count < max_processes && i <= MAX_PROCESSES; i++) {
        if (proc_table[i] != NULL) {
            k_ptr[count].pid = i;
            k_ptr[count].size = proc_table[i]->size;
            k_ptr[count].stime = (time_stamp() - proc_table[i]->create_time)/1000;
            strncpy(k_ptr[count].command, proc_table[i]->command, N_NAME);
            count++;
        }
    }

    cpa->usr_ptr = (seL4_Word) processes;
    cpa->src = (seL4_Word) k_ptr;
    cpa->nbyte = count * sizeof(sos_process_t);
    cpa->count = 0;
    cpa->cb = process_status_cb;

    copy_out(pid, reply_cap, cpa);
}

void process_status_cb(int pid, seL4_CPtr reply_cap, void *_args) {
    copy_out_args *args = (copy_out_args *) _args;
    send_seL4_reply(reply_cap, ((seL4_Word) args->count) /sizeof(sos_process_t));
    free((void *) args->src);
    printf("ps done\n");
}
