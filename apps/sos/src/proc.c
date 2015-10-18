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
int free_pids[MAX_PROCESSES + 1];
int next_pid = 1;
wait_list *wait_head;

int num_processes = 0;
void process_status_cb(int pid, seL4_CPtr reply_cap, void *_args, int err);
void start_process_cb1(int new_pid, seL4_CPtr reply_cap, void *args, int err);
void start_process_cb2(int pid, seL4_CPtr reply_cap, void *args, int err);
void start_process_cb_cont(int pid, seL4_CPtr reply_cap, void *args, int err);

void proc_table_init(void) {
    memset(proc_table, 0, (MAX_PROCESSES + 1) * sizeof(addr_space*));
    for (int i = 1; i < MAX_PROCESSES; i++) {
        free_pids[i] = i+1;
    }
    free_pids[0] = 0;
    free_pids[MAX_PROCESSES] = 0;
    wait_head = NULL;
}

void new_as(int pid, seL4_CPtr reply_cap, void *_args) {
    printf("new as called with parent %d \n", pid);

    new_as_args *args = (new_as_args *) _args;
    if (num_processes == MAX_PROCESSES) {
        eprintf("Error caught in new_as\n");
        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    int new_pid = next_pid;
    if (!new_pid) {
        eprintf("Error caught in new_as\n");
        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    //pop the stack of pids
    next_pid = free_pids[next_pid];
    free_pids[new_pid] = 0;
    num_processes++;

    addr_space *as = malloc(sizeof(addr_space)); 
    if (as == NULL) {
        //nothing to be done here
        eprintf("Error caught in new_as\n");

        //push pid back onto stack
        free_pids[new_pid] = next_pid; 
        next_pid = new_pid;
        num_processes--;

        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    //initialise all values to 0
    memset(as, 0, sizeof(addr_space));
    proc_table[new_pid] = as;
    as->parent_pid = pid;

    int err = fdt_init(new_pid);
    if (err) {

        eprintf("Error caught in new_as\n");

        cleanup_as(new_pid);

        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
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

    if (vm_args == NULL) {
        //nothing to be done here
        eprintf("Error caught in new_as\n");

        cleanup_as(new_pid);

        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    args->new_pid = new_pid;
    vm_args->cb = args->cb;
    vm_args->cb_args = args->cb_args;

    //no longer need this 
    free(args);

    //this can block
    vm_init(new_pid, reply_cap, vm_args);
    //vm init will call start_process_cb1 after this
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
    printf("Destroying tcb\n");
    if (as->tcb_addr) {
        ut_free(as->tcb_addr, seL4_TCBBits);
        cspace_revoke_cap(cur_cspace, as->tcb_cap);
        cspace_delete_cap(cur_cspace, as->tcb_cap);
        as->tcb_cap = 0;
        as->tcb_addr = 0;
    }
    printf("tcb destroyed\n");

    if (as->ipc_buffer_addr) {
        cspace_revoke_cap(cur_cspace, as->ipc_buffer_cap);
        cspace_delete_cap(cur_cspace, as->ipc_buffer_cap);
        as->ipc_buffer_addr = 0;
        as->ipc_buffer_cap = 0;
    }

    if (as->croot) {
        cspace_destroy(as->croot);
    }
    
    if (as->vroot_addr) {
        ut_free(as->vroot_addr, seL4_PageDirBits);
        cspace_revoke_cap(cur_cspace, as->vroot);
        cspace_delete_cap(cur_cspace, as->vroot);
        as->vroot = 0;
        as->vroot_addr = 0;
    }


    child_proc *cur_child = as->children;
    while (as->children != NULL) {
         as->children = cur_child->next;
         free(cur_child);
         cur_child = as->children;
    }

    free(as);

    proc_table[pid] = NULL;
    
    free_pids[pid] = next_pid;
    next_pid = pid;

    num_processes--;
} 

void start_process(int parent_pid, seL4_CPtr reply_cap, void *_args) {
    if (TMP_DEBUG) printf("start_process\n");
    printf("starting process from pid %d\n", parent_pid);
	start_process_args *args = (start_process_args *) _args;

    //check parent exists 
    addr_space *parent_as = proc_table[parent_pid];

    if (parent_pid > 0 && parent_as == NULL) {
        //this shouldn't happen
        assert(0);
    }
	
    // Get args that we use
	// Get new pid/make new address space
    new_as_args *as_args = malloc(sizeof(new_as_args));
    if (as_args == NULL) {
        eprintf("Error caught in start_process\n"); 
        args->cb(parent_pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    as_args->cb = start_process_cb1;
    as_args->cb_args = args;
    new_as(parent_pid, reply_cap, as_args);

    if (TMP_DEBUG) printf("start_process end\n");
}

void start_process_cb1(int new_pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (TMP_DEBUG) printf("start_process_cb1\n");

    start_process_args *args = (start_process_args *) _args;
    // Get args that we use
    char *app_name = args->app_name;

    if (err) {
        //if the kernel can't create a process, die
        eprintf("Error caught in start_process_cb1\n");

        assert(!args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        free(args);
        cleanup_as(new_pid);
        return;
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
            eprintf("Error caught in start_process_cb1\n");
            
            assert(!args->parent_pid);
            args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
            free(args);
            cleanup_as(new_pid);
            return;
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
        eprintf("Error caught in start_process_cb1: couldn't create vspace\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        free(args);
        cleanup_as(new_pid);
        return;
    }   

    err = cspace_ut_retype_addr(as->vroot_addr
                                   ,seL4_ARM_PageDirectoryObject
                                   ,seL4_PageDirBits
                                   ,cur_cspace
                                   ,&(as->vroot)
                                   );
    if (err) {
        eprintf("Error caught in start_process_cb1: couldn't retype for vspace\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        free(args);
        cleanup_as(new_pid);
        return;
    }

    /* Create a simple 1 level CSpace */
    as->croot = cspace_create(1);
    if (as->croot == NULL) {
        eprintf("Error caught in start_process_cb1: couldn't create cspace\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        free(args);
        cleanup_as(new_pid);
        return;
    }

    // Set up frame_alloc_swap args
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    if (alloc_args == NULL) {
        eprintf("Error caught in start_process_cb1\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        free(args);
        cleanup_as(new_pid);
        return;
    }

    alloc_args->map = NOMAP;
    alloc_args->cb = start_process_cb2;
    alloc_args->cb_args = args;

    /* Get IPC buffer */
    frame_alloc_swap(new_pid, reply_cap, alloc_args, 0);
    if (TMP_DEBUG) printf("start_process_cb1 end\n");
}

void start_process_cb2(int new_pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (TMP_DEBUG) printf("start_process_cb2\n");
	frame_alloc_args *alloc_args = (frame_alloc_args *) _args;
	start_process_args *args = (start_process_args *) alloc_args->cb_args;

	// Free frame_alloc args
	int index = alloc_args->index;

	free(alloc_args);
    alloc_args = NULL;
	
    if (err || index == FRAMETABLE_ERR) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }

	// Various local state
	// Address space of the new process
	addr_space *as = proc_table[new_pid];
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
    
    printf("cap: %p\n", (void *)as->ipc_buffer_cap);
    printf("err :%d\n", err);

    if (err) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }

    printf("Setting index %p to don't swap\n", (void *) index);
    frametable[index].frame_status |= FRAME_DONT_SWAP;

    /* Copy the fault endpoint to the user app to enable IPC */
    if (TMP_DEBUG) printf("PID is %d\n", new_pid);
    user_ep_cap = cspace_mint_cap(as->croot
                                 ,cur_cspace
                                 ,args->fault_ep
                                 ,seL4_AllRights 
                                 ,seL4_CapData_Badge_new(new_pid)
                                 );

    //???
    /* should be the first slot in the space, hack I know */
    //assert(user_ep_cap == 1);
    //assert(user_ep_cap == USER_EP_CAP);
    
    /* Create a new TCB object */
    as->tcb_addr = ut_alloc(seL4_TCBBits);
    if (!as->tcb_addr) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }

    err =  cspace_ut_retype_addr(as->tcb_addr,
                                 seL4_TCBObject,
                                 seL4_TCBBits,
                                 cur_cspace,
                                 &(as->tcb_cap));
    if (err) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(as->tcb_cap, user_ep_cap, as->priority,
                             as->croot->root_cnode, seL4_NilData,
                             as->vroot, seL4_NilData, PROCESS_IPC_BUFFER,
                             as->ipc_buffer_cap);

    if (err) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }

    /* parse the cpio image */
    dprintf(1, "\nStarting \"%s\"...\n", as->command);
    args->elf_base = cpio_get_file(_cpio_archive, as->command, &elf_size);
    printf("tried to do cpio_get_file\n");
    if (!args->elf_base) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }

    /* load the elf image */
    elf_load_args *load_args = malloc(sizeof(elf_load_args));
    if (load_args == NULL) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }
    load_args->elf_file = args->elf_base;
    load_args->curr_header = 0;
    load_args->cb = start_process_cb_cont;
    load_args->cb_args = args;
    printf("reply cap %p\n",(void *) reply_cap);
    elf_load(new_pid, reply_cap, load_args, 0);
    
    if (TMP_DEBUG) printf("start_process_cb2 end\n");
}

void start_process_cb_cont(int pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (TMP_DEBUG) printf("start_process_cb_cont\n");
    printf("reply cap %p\n",(void *) reply_cap);
    start_process_args *args = (start_process_args *) _args;

    if (err) {
        eprintf("Error caught in start_process_cb_cont\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(pid);
        return;
    }   

    /* These required for setting up the TCB */
    seL4_UserContext context;
    /* Start the new process */
    addr_space *as = proc_table[pid];
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(args->elf_base);
    context.sp = PROCESS_STACK_TOP;
    seL4_TCB_WriteRegisters(as->tcb_cap, 1, 0, 2, &context);
    
    args->cb(args->parent_pid, reply_cap, (void *)pid, 0);
    free(args);

    if (TMP_DEBUG) printf("start_process_cb_cont end\n");
}

void process_status(seL4_CPtr reply_cap
                   ,int pid
                   ,sos_process_t* processes
                   ,unsigned max_processes
                   ) {
    if (processes == NULL) {
        send_seL4_reply(reply_cap, pid, 0);
    }
    
    //change this to a min between max and current
    sos_process_t* k_ptr = malloc(sizeof(sos_process_t) * max_processes);
    if (k_ptr == NULL) {
        send_seL4_reply(reply_cap, pid, 0);
    }
    
    copy_out_args *cpa = malloc(sizeof(copy_out_args));
    if (cpa == NULL) {
        free(k_ptr);
        send_seL4_reply(reply_cap, pid, 0); 
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

    copy_out(pid, reply_cap, cpa, 0);
}

void process_status_cb(int pid, seL4_CPtr reply_cap, void *args, int err) {
    copy_out_args *copy_args = (copy_out_args *) args;
    int count = ((seL4_Word) copy_args->count) /sizeof(sos_process_t);
    free((void *) copy_args->src);
    free(args);
    if (err) {
        eprintf("Error caught in process_status_cb\n");
        count = 0;
    }
    send_seL4_reply(reply_cap, pid, count); 
    printf("ps done\n");
}

void handle_process_create_cb (int pid, seL4_CPtr reply_cap, void *args, int err) {
    if (TMP_DEBUG) printf("handle_process_create_cb\n");
    if (!err) {
        printf("Process %d\n", pid);
    }
    int child_pid = (int) args;
    if (err) {
        send_seL4_reply(reply_cap, pid, err);
    } else {
        send_seL4_reply(reply_cap, pid, child_pid);
    }  
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

void remove_child(int parent_pid, int child_pid) {
    if (SOS_DEBUG) printf("remove_child\n");
    //we know at this point the parent actually has children
    child_proc *cur = proc_table[parent_pid]->children;
    
    //check if the head of the children is the child to delete
    if (cur->pid == child_pid) {
        proc_table[parent_pid]->children = cur->next; 
        free(cur);
        return;
    }

    //get the child before the child to delete
    while (cur->next != NULL && cur->next->pid != child_pid) {
        cur = cur->next; 
    }
    
    //cur->next is the child to delete
    child_proc *tmp = cur->next;

    if (tmp != NULL) {
        cur->next = tmp->next;
        free(tmp);
    }
    if (SOS_DEBUG) printf("remove_child ended\n");
}

void kill_process(int pid, int to_delete, seL4_CPtr reply_cap) {
    //check we have something to delete 
    printf("kill_process cap: %p\n", (void *) reply_cap);
    if (proc_table[to_delete] == NULL) {
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }

    if (to_delete == pid) {
        printf("Deleting self\n");
        int parent_pid = proc_table[to_delete]->parent_pid;
        if (parent_pid && proc_table[parent_pid]->wait_cap) {
            send_seL4_reply(proc_table[parent_pid]->wait_cap, parent_pid, 0);
            proc_table[parent_pid]->wait_cap = 0;
        }
        proc_table[to_delete]->status &= ~PROC_BLOCKED;
    } else if (is_child(pid, to_delete)) {
        remove_child(pid, to_delete);
    } else {
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }

    addr_space *as = proc_table[to_delete];

    as->status |= PROC_DYING;
    as->wait_cap = reply_cap;
    //increment the wait count in the parent 
    int parent = proc_table[to_delete]->parent_pid;
    if (parent != 0) {
        proc_table[parent]->delete_wait++;
    }
    //kill all its children first 
    child_proc *cur = as->children;
    while (cur != NULL) {
        kill_process(pid, cur->pid, reply_cap);
        free(cur);
        cur = cur->next;
    }

    as->delete_reply_cap = reply_cap;
    as->delete_pid = pid;

    //child is ready to be killed 
    if (!(as->status & PROC_BLOCKED)) { 
        if (SOS_DEBUG) printf("Calling kill_process_cb\n");
        kill_process_cb(pid, reply_cap, (void *) to_delete, 0);
    }
}

void kill_process_cb(int pid, seL4_CPtr reply_cap, void *data, int err) {
    if (SOS_DEBUG) printf("kill_process_cb\n");
    if (err) {
        //i don't think this can happen
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    int to_delete = (int) data;
    int parent = proc_table[to_delete]->parent_pid;
    int current = to_delete;
    

    //if we aren't trying to delete ourself and the parent is done waiting on 
    //dying processes, reply to the parent
    printf("cb cap: %p\n", (void *) reply_cap);
    while (parent != 0 
           && (proc_table[parent]->status & PROC_DYING) 
           && (--proc_table[parent]->delete_wait == 0)) {
        current = parent;
        parent = proc_table[parent]->parent_pid;
    } 
    seL4_CPtr cap = proc_table[current]->wait_cap;
    proc_table[current]->wait_cap = 0;
    if (cap && parent != 0) {
        send_seL4_reply(cap, parent, 0);
    }
    //destroy the address space of the process
    cleanup_as(to_delete);
    if (SOS_DEBUG) printf("kill_process_cb ended\n");
}

void add_to_wait_list(int pid) {
    wait_list *new_wait = malloc(sizeof(wait_list));
    new_wait->pid = pid;
    new_wait->next = wait_head;
    wait_head = new_wait;
}

void wake_wait_list(int pid) {
    wait_list *curr_wait = wait_head;
    wait_list *next_wait;
    int wake_pid;
    //9242_TODO Check for dead process
    while (curr_wait != NULL) {
        wake_pid = wait_head->pid;
        next_wait = wait_head->next;
        send_seL4_reply(proc_table[wake_pid]->wait_cap, wake_pid, pid);
        proc_table[wake_pid]->wait_cap = 0;
        free(curr_wait);
        curr_wait = next_wait;
    }
    wait_head = NULL;
}
