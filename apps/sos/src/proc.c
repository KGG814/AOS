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
#include "debug.h"

extern char _cpio_archive[];

addr_space* proc_table[MAX_PROCESSES + 1];
int num_processes = 0;
int next_pid = MAX_PROCESSES;

void process_status_cb(int pid, seL4_CPtr reply_cap, void *_args);
void start_process_cb1(int new_pid, seL4_CPtr reply_cap, void *args);
void start_process_cb2(int pid, seL4_CPtr reply_cap, void *args);
void start_process_cb_cont(int pid, seL4_CPtr reply_cap, void *args);

void proc_table_init(void) {
    memset(proc_table, 0, (MAX_PROCESSES + 1) * sizeof(addr_space*));
}

void new_as(int pid, seL4_CPtr reply_cap, void *_args) {
    new_as_args *args = (new_as_args *) _args;
    if (num_processes == MAX_PROCESSES) {
        args->new_pid = PROC_ERR;
        return;
    }

    int new_pid = next_pid;
    for (int i = 0; i < MAX_PROCESSES && proc_table[next_pid] != NULL; i++) {
        next_pid = (next_pid % MAX_PROCESSES) + 1;
    }

    pid = next_pid;
    next_pid = (next_pid % MAX_PROCESSES) + 1;

    addr_space *as = malloc(sizeof(addr_space)); 
    memset(as, 0, sizeof(addr_space));
    if (as == NULL) {
        //really nothing to be done
        args->new_pid = PROC_ERR;
        return;
    }
    memset(as, 0, sizeof(addr_space));
    proc_table[new_pid] = as;
    
    int err = fdt_init(new_pid);
    if (err) {
        free(as);
        args->new_pid = PROC_ERR;
        return;
    }
    as->status = 0;
    as->pid = new_pid;
    as->size = 0;
    as->create_time = time_stamp();
    
    num_processes++;
    vm_init_args* vm_args = malloc(sizeof(vm_init_args));
    args->new_pid = new_pid;
    vm_args->cb = args->cb;
    vm_args->cb_args = args->cb_args;
    vm_init(new_pid, reply_cap, vm_args);
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
    //9242_TODO, cleanup vroot, ipc, tcb, croot

    free(as);

    proc_table[pid] = NULL;
    num_processes--;
} 

void start_process(int parent_pid, seL4_CPtr reply_cap, void *_args) {
    if (TMP_DEBUG) printf("start_process\n");
	start_process_args *args = (start_process_args *) _args;
    if (args == NULL && reply_cap) {
        send_seL4_reply(reply_cap, -1);
    }

    //check parent exists 
    addr_space *parent_as = proc_table[parent_pid];

    if (parent_pid > 0 && parent_as == NULL) {
        //this shouldn't happen
        assert(0);
    }
	
    // Get args that we use
	char *app_name = args->app_name;
	// Get new pid/make new address space
    new_as_args *as_args = malloc(sizeof(new_as_args));
    as_args->cb = start_process_cb1;
    as_args->cb_args = args;
    new_as(parent_pid, reply_cap, as_args);
    if (TMP_DEBUG) printf("start_process end\n");
}

void start_process_cb1(int new_pid, seL4_CPtr reply_cap, void *_args) {
    if (TMP_DEBUG) printf("start_process_cb1\n");
    start_process_args *args = (start_process_args *) _args;
    // Get args that we use
    char *app_name = args->app_name;
    if (new_pid == PROC_ERR) {
        if (reply_cap) {
            assert(RTN_ON_FAIL);
            send_seL4_reply(reply_cap, -1);
        } else {
            //this will only be entered if we are in start_first_process and 
            //malloc fails
            assert(0); 
        }
    }
    if (TMP_DEBUG) printf("pid: %d\n", new_pid);
    // Address space of process
    addr_space* as = proc_table[new_pid];

    //do some preliminary initialisation
    as->status = 0;
    as->priority = args->priority;
    as->parent_pid = args->parent_pid;
    as->pid = new_pid; 
    as->size = 0;
    as->create_time = time_stamp();
    as->children = NULL;

    if (parent_pid) {
        child_proc *new = malloc(sizeof(child_proc));
        if (new == NULL) {
            if (reply_cap) {
                free(args);
                cleanup_as(new_pid);
                send_seL4_reply(reply_cap, -1);
            } else {
                assert(!"somehow managed to try and add a child to rootserver");
            }
        }

        new->pid = new_pid;
        new->next = parent_as->children;
        parent_as->children = new;
    }

    memset(as->command, 0, N_NAME);
    strncpy(as->command, app_name, N_NAME - 1);

    /* Create a VSpace */
    as->vroot_addr = ut_alloc(seL4_PageDirBits);
    if (!as->vroot_addr) {
        if (reply_cap) {
            cleanup_as(new_pid);
            free(args);
            send_seL4_reply(reply_cap, -1);
        } else {
            assert(!"couldn't allocate memory for process vspace");
        }
    }   

    int err = cspace_ut_retype_addr(as->vroot_addr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &(as->vroot));
    if (err) {
        if (reply_cap) {
            cleanup_as(new_pid);
            free(args);
            send_seL4_reply(reply_cap, -1);
        } else {
            assert(!"Failed to allocate page directory cap for client");
        }
    }

    /* Create a simple 1 level CSpace */
    as->croot = cspace_create(1);
    if (as->croot == NULL) {
        if (reply_cap) {
            cleanup_as(new_pid);
            free(args);
            send_seL4_reply(reply_cap, -1);
        } else {
            assert(!"Failed to allocate page directory cap for client");
        }
    }

    // Set up frame_alloc_swap args
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    if (alloc_args == NULL) {
        if (reply_cap) {
            cleanup_as(new_pid);
            free(args);
            send_seL4_reply(reply_cap, -1);
        } else {
            //this will only be entered if we are in start_first_process and 
            //malloc fails
            assert(0); 
        }
    }

    alloc_args->map = NOMAP;
    alloc_args->cb = start_process_cb2;
    alloc_args->cb_args = args;

    /* Get IPC buffer */
    frame_alloc_swap(new_pid, reply_cap, alloc_args);
    if (TMP_DEBUG) printf("start_process_cb1 end\n");
}

void start_process_cb2(int new_pid, seL4_CPtr reply_cap, void *args) {
    if (TMP_DEBUG) printf("start_process_cb2\n");
	frame_alloc_args *alloc_args = (frame_alloc_args *) args;
	start_process_args *process_args = (start_process_args *) alloc_args->cb_args;
    if (alloc_args->index < 0) {
        free(process_args);
        free(alloc_args);
        cleanup_as(new_pid);
        return;
    }

	/* Get args */
	// Address space of the new process
	addr_space *as = proc_table[new_pid];

	// Free frame_alloc args
	int index = alloc_args->index;
	free(alloc_args);

	
	// Various local state
	int err;
	seL4_CPtr user_ep_cap;

	// These required for loading program sections
    unsigned long elf_size;
	
	as->ipc_buffer_addr = index_to_paddr(index);
    as->ipc_buffer_cap = frametable[index].frame_cap;
    /* Map IPC buffer*/
    err = map_page_user(as->ipc_buffer_cap, as->vroot,
                   PROCESS_IPC_BUFFER,
                   seL4_AllRights, seL4_ARM_Default_VMAttributes, as);
    frametable[index].frame_status |= FRAME_DONT_SWAP;
    conditional_panic(err, "Unable to map IPC buffer for user app");

    /* Copy the fault endpoint to the user app to enable IPC */
   if (TMP_DEBUG) printf("PID is %d\n", new_pid);
    user_ep_cap = cspace_mint_cap(as->croot
                                 ,cur_cspace
                                 ,process_args->fault_ep
                                 ,seL4_AllRights 
                                 ,seL4_CapData_Badge_new(new_pid)
                                 );

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
    err = seL4_TCB_Configure(as->tcb_cap, user_ep_cap, as->priority,
                             as->croot->root_cnode, seL4_NilData,
                             as->vroot, seL4_NilData, PROCESS_IPC_BUFFER,
                             as->ipc_buffer_cap);
    conditional_panic(err, "Unable to configure new TCB");

    /* parse the cpio image */
    dprintf(1, "\nStarting \"%s\"...\n", as->command);
    process_args->elf_base = cpio_get_file(_cpio_archive, as->command, &elf_size);
    conditional_panic(!process_args->elf_base, "Unable to locate cpio header");
    /* load the elf image */
    elf_load_args *load_args = malloc(sizeof(elf_load_args));
    load_args->elf_file = process_args->elf_base;
    load_args->curr_header = 0;
    load_args->cb = start_process_cb_cont;
    load_args->cb_args = process_args;
    printf("reply cap %p\n",(void *) reply_cap);
    elf_load(new_pid, reply_cap, load_args);
    
    if (TMP_DEBUG) printf("start_process_cb2 end\n");
}

void start_process_cb_cont(int pid, seL4_CPtr reply_cap, void *_args) {
    if (TMP_DEBUG) printf("start_process_cb_cont\n");
    printf("reply cap %p\n",(void *) reply_cap);
    start_process_args *args = (start_process_args *) _args;
    /* These required for setting up the TCB */
    seL4_UserContext context;
    /* Start the new process */
    addr_space *as = proc_table[pid];
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(args->elf_base);
    context.sp = PROCESS_STACK_TOP;
    seL4_TCB_WriteRegisters(as->tcb_cap, 1, 0, 2, &context);
    if (args->cb) {
        args->cb(pid, reply_cap, args->cb_args);
    }
    if (TMP_DEBUG) printf("start_process_cb_cont end\n");
}

void process_status(seL4_CPtr reply_cap
                   ,int pid
                   ,sos_process_t* processes
                   ,unsigned max_processes
                   ) {
    if (processes == NULL) {
        send_seL4_reply(reply_cap, 0);
    }
    
    //change this to a min between max and current
    sos_process_t* k_ptr = malloc(sizeof(sos_process_t) * max_processes);
    if (k_ptr == NULL) {
        send_seL4_reply(reply_cap, 0);
    }
    
    copy_out_args *cpa = malloc(sizeof(copy_out_args));
    if (cpa == NULL) {
        free(k_ptr);
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

void process_status_cb(int pid, seL4_CPtr reply_cap, void *args) {
    copy_out_args *copy_args = (copy_out_args *) args;
    send_seL4_reply(reply_cap, ((seL4_Word) copy_args->count) /sizeof(sos_process_t));
    free((void *) copy_args->src);
    free(args);
    printf("ps done\n");
}

void handle_process_create_cb (int pid, seL4_CPtr reply_cap, void *args) {
    if (TMP_DEBUG) printf("handle_process_create_cb\n");
    send_seL4_reply(reply_cap, pid);
    if (TMP_DEBUG) printf("handle_process_create_cb ended\n\n\n\n");
}