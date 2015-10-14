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
    printf("new as called with parent %d \n", pid);
    new_as_args *args = (new_as_args *) _args;
    if (num_processes == MAX_PROCESSES) {
        args->new_pid = PROC_ERR;
        return;
    }

    int new_pid = next_pid;
    for (int i = 0; i < MAX_PROCESSES && proc_table[next_pid] != NULL; i++) {
        next_pid = (next_pid % MAX_PROCESSES) + 1;
    }

    next_pid = (next_pid % MAX_PROCESSES) + 1;
    num_processes++;

    addr_space *as = malloc(sizeof(addr_space)); 
    if (as == NULL) {
        //really nothing to be done
        args->new_pid = PROC_ERR;
        return;
    }
    memset(as, 0, sizeof(addr_space));
    proc_table[new_pid] = as;
    as->parent_pid = pid;
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
    
    as->wait_cap = 0;
    as->reader_status = NO_READ;

    as->delete_wait = 0;

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
    //9242_TODO, cleanup vm?
    //9242_TODO, cleanup vroot, ipc, tcb, croot

    free(as);

    proc_table[pid] = NULL;
    num_processes--;
} 

void start_process(int parent_pid, seL4_CPtr reply_cap, void *_args) {
    if (TMP_DEBUG) printf("start_process\n");
    printf("starting process from pid %d\n", parent_pid);
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
    // Address space of parent 
    addr_space* parent_as = proc_table[args->parent_pid];

    //do some preliminary initialisation
    as->status = 0;
    as->priority = args->priority;
    as->pid = new_pid; 
    as->size = 0;
    as->create_time = time_stamp();
    as->children = NULL;
    as->status = PROC_READY;
    if (as->parent_pid != 0) {
        child_proc *new = malloc(sizeof(child_proc));
        if (new == NULL) {
            if (reply_cap) {
                free(args);
                cleanup_as(new_pid);
                send_seL4_reply(reply_cap, -1);
                return;
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
        if (!reply_cap) {
            assert(!"couldn't allocate memory for first process");
        }
        if (process_args) {
            free(process_args);
        }
        if (alloc_args) {
            free(alloc_args);
        }
        cleanup_as(new_pid);
        send_seL4_reply(reply_cap, -1);
        return;
    }

	/* Get args */
	// Address space of the new process
	addr_space *as = proc_table[new_pid];

	// Free frame_alloc args
	int index = alloc_args->index;
	free(alloc_args);
    alloc_args = NULL;
	
	// Various local state
	int err;
	seL4_CPtr user_ep_cap;

	// These required for loading program sections
    unsigned long elf_size;
	
	as->ipc_buffer_addr = index_to_paddr(index);
    /* Map IPC buffer*/
    as->ipc_buffer_cap = cspace_copy_cap(cur_cspace
                                   ,cur_cspace
                                   ,frametable[index].frame_cap
                                   ,seL4_AllRights
                                   );

    err = map_page_user(as->ipc_buffer_cap, as->vroot,
                   PROCESS_IPC_BUFFER,
                   seL4_AllRights, seL4_ARM_Default_VMAttributes, as);
    printf("Setting index %p to don't swap\n", (void *) index);
    frametable[index].frame_status |= FRAME_DONT_SWAP;
    printf("cap: %p\n", (void *)as->ipc_buffer_cap);
    printf("err :%d\n", err);
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
    printf("tried to do cpio_get_file\n");
    if (!process_args->elf_base) {
        if (!reply_cap) {
            assert(!"Unable to load cpio header");
        }
        printf("trying to do cleanup\n");
        free(process_args);
        cleanup_as(new_pid);
        send_seL4_reply(reply_cap, -1);
        return;
    }

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
    printf("Process %d created with parent pid %d\n", pid, proc_table[pid]->parent_pid);
    send_seL4_reply(reply_cap, pid);
    if (TMP_DEBUG) printf("handle_process_create_cb ended\n\n\n\n");
}

int is_child(int parent_pid, int child_pid) {
    if (proc_table[parent_pid] == NULL || proc_table[child_pid] == NULL) {
        return 0;
    } 

    //determine if child's internal parent is indeed parent 
    int ret = (proc_table[child_pid]->parent_pid == parent_pid) ? 1 : 0;

    //determine if child is indeed a child of the parent
    if (ret) {
        child_proc *cur = proc_table[parent_pid]->children;
        while (cur != NULL) {
            if (cur->pid == child_pid) {
                return 1; 
            }
            cur = cur->next;
        }
    }

    return 0;
}

int remove_child(int parent_pid, int child_pid) {
    if (!is_child(parent_pid, child_pid)) {
        return 0;
    }

    //we know at this point the parent actually has children
    child_proc *cur = proc_table[parent_pid]->children;
    
    //check if the head of the children is the child to delete
    if (cur->pid == child_pid) {
        proc_table[parent_pid]->children = cur->next; 
        free(cur);
        return 1;
    }

    //get the child before the child to delete
    while (cur->next != NULL && cur->next->pid != child_pid) {
        cur = cur->next; 
    }
    
    //cur->next is the child to delete
    child_proc *tmp = cur->next;
    cur->next = tmp->next;
    free(tmp);

    return 1;
}

void kill_process(int delete_pid, int child_pid, seL4_CPtr reply_cap) {
    if (proc_table[child_pid] == NULL) {
        return;
    }

    addr_space *as = proc_table[child_pid];

    as->status |= PROC_DYING;

    //increment the wait count in the parent 
    if (proc_table[delete_pid]) {
        proc_table[delete_pid]->delete_wait++;
    }
    //kill all its children first 
    child_proc *cur = as->children;
    while (cur != NULL) {
        kill_process(delete_pid, cur->pid, reply_cap);
        free(cur);
        cur = cur->next;
    }

    as->delete_reply_cap = reply_cap;
    as->delete_pid = delete_pid;

    //child is ready to be killed 
    if (!(as->status & PROC_BLOCKED)) { 
        kill_process_cb(delete_pid, reply_cap, (void *) child_pid);
    }
}

void kill_process_cb(int delete_pid, seL4_CPtr reply_cap, void *data) {
    //destroy the address space of the process
    cleanup_as((int) data);

    //if we aren't trying to delete ourself and the parent is done waiting on 
    //dying processes, reply to the parent
    if (delete_pid //make sure parent is not the OS
    && (delete_pid != (int) data)
    && (--proc_table[delete_pid]->delete_wait == 0)) {
        send_seL4_reply(reply_cap, 0);
    } 
}
