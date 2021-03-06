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

//circular buffer of free pids
int free_pids[MAX_PROCESSES];

int free_pids_start;
int free_pids_end;

wait_list *wait_head;

int num_processes = 0;
void process_status_cb(int pid, seL4_CPtr reply_cap, void *_args, int err);
void start_process_cb1(int new_pid, seL4_CPtr reply_cap, void *args, int err);
void start_process_cb2(int pid, seL4_CPtr reply_cap, void *args, int err);
void start_process_cb_cont(int pid, seL4_CPtr reply_cap, void *args, int err);

void check_elf_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void elf_get_frames (int pid, seL4_CPtr reply_cap, start_process_args *args, int err);
void elf_frame_alloc (int pid, seL4_CPtr reply_cap, frame_alloc_args *alloc_args, int err);
void process_init(int new_pid, seL4_CPtr reply_cap, void *_args, int err) ;
void process_init_cb(int new_pid, seL4_CPtr reply_cap, void *_args, int err);
void process_init_cb2(int new_pid, seL4_CPtr reply_cap, void *_args, int err);
void elf_load_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);
void cleanup_elf_table(start_process_args *args);

void proc_table_init(void) {
    memset(proc_table, 0, (MAX_PROCESSES + 1) * sizeof(addr_space*));
    for (int i = 0; i < MAX_PROCESSES; i++) {
        free_pids[i] = i+1;
    }
    free_pids_start = 0;
    free_pids_end = 0;
    wait_head = NULL;
}

void new_as(int pid, seL4_CPtr reply_cap, void *_args) {
    if (SOS_DEBUG) printf("new as called with parent %d \n", pid);

    new_as_args *args = (new_as_args *) _args;
    if (num_processes == MAX_PROCESSES) {
        eprintf("Error caught in new_as\n");
        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    //get the next free pid from the queue
    int new_pid = free_pids[free_pids_start];
    if (!new_pid) {
        eprintf("Error caught in new_as\n");
        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    addr_space *as = malloc(sizeof(addr_space)); 
    if (as == NULL) {
        //nothing to be done here
        eprintf("Error caught in new_as\n");

        args->cb(pid, reply_cap, args->cb_args, -1);
        free(args);
        return;
    }

    //pop the queue of pids
    free_pids[free_pids_start] = 0;
    free_pids_start = (free_pids_start + 1) % MAX_PROCESSES;
    num_processes++;

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
    assert(num_processes);
    if (pid < 1 || pid > MAX_PROCESSES) {
        //invalid pid
        return;
    }

    addr_space *as = proc_table[pid];
    if (as == NULL) {
        if (SOS_DEBUG) printf("Tried to cleanup non-existent pid %d\n", pid);
        //nonexistent as 
        return;
    }

    if (SOS_DEBUG) printf("Cleaning up pid %d\n", pid);
    fdt_cleanup(pid);
    pt_cleanup(pid);

    if (SOS_DEBUG) printf("Destroying tcb\n");
    if (as->tcb_addr) {
        ut_free(as->tcb_addr, seL4_TCBBits);
        cspace_revoke_cap(cur_cspace, as->tcb_cap);
        cspace_delete_cap(cur_cspace, as->tcb_cap);
        as->tcb_cap = 0;
        as->tcb_addr = 0;
    }
    if (SOS_DEBUG) printf("tcb destroyed\n");

    if (as->ipc_buffer_addr) {
        cspace_revoke_cap(cur_cspace, as->ipc_buffer_cap);
        cspace_delete_cap(cur_cspace, as->ipc_buffer_cap);
        as->ipc_buffer_addr = 0;
        as->ipc_buffer_cap = 0;
    }
    if (SOS_DEBUG) printf("Destroying cspace\n");
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
    if (SOS_DEBUG) printf("Freeing children\n");
    while (as->children != NULL) {
        if (SOS_DEBUG) printf("Freeing child %d\n", cur_child->pid);
        as->children = cur_child->next;
        
        free(cur_child);
        cur_child = as->children;
    }
    if (SOS_DEBUG) printf("Freeing addr space\n");
    free(as);

    proc_table[pid] = NULL;
    
    //put the old pid into the stack of free pids
    free_pids[free_pids_end] = pid;
    free_pids_end = (free_pids_end + 1) % MAX_PROCESSES;
    num_processes--;
    if (SOS_DEBUG) printf("Cleanup as ended\n");
} 

void start_process_wrapper(int pid, seL4_CPtr reply_cap, void *data, int err) {
    start_process_args* process_args = (start_process_args *) data;
    if (err) {
        eprintf("Error caught in start_process_wrapper\n");
        process_args->cb(pid, reply_cap, data, err);
    }

    if (SOS_DEBUG) printf("Starting process %s\n", (char *) process_args->app_name);
    //start_process(pid, reply_cap, process_args);
    start_process_load(pid, reply_cap, process_args);
}

void start_process(int parent_pid, seL4_CPtr reply_cap, void *_args) {
    if (TMP_DEBUG) printf("start_process\n");
    if (SOS_DEBUG) printf("starting process from pid %d\n", parent_pid);
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

    if (SOS_DEBUG) printf("start_process end\n");
}

void start_process_cb1(int new_pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (SOS_DEBUG) printf("start_process_cb1\n");

    start_process_args *args = (start_process_args *) _args;
    // Get args that we use
    char *app_name = args->app_name;

    if (err) {
        //if the kernel can't create a process, die
        eprintf("Error caught in start_process_cb1\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        free(args);
        cleanup_as(new_pid);
        return;
    }

    if (SOS_DEBUG) printf("pid: %d\n", new_pid);
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
            
            assert(args->parent_pid);
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
    alloc_args->swap = NOT_SWAPPABLE;
    alloc_args->cb = start_process_cb2;
    alloc_args->cb_args = args;

    /* Get IPC buffer */
    frame_alloc_swap(new_pid, reply_cap, alloc_args, 0);
    if (SOS_DEBUG) printf("start_process_cb1 end\n");
}

void start_process_cb2(int new_pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (SOS_DEBUG) printf("start_process_cb2\n");
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
    
    if (SOS_DEBUG) printf("cap: %p\n", (void *)as->ipc_buffer_cap);
    if (SOS_DEBUG) printf("err :%d\n", err);

    if (err) {
        eprintf("Error caught in start_process_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);

        free(args);
        cleanup_as(new_pid);
        return;
    }

    if (SOS_DEBUG) printf("Setting index %p to don't swap\n", (void *) index);
    frametable[index].frame_status |= FRAME_DONT_SWAP;

    /* Copy the fault endpoint to the user app to enable IPC */
    if (SOS_DEBUG) printf("PID is %d\n", new_pid);
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
    if (SOS_DEBUG) printf("tried to do cpio_get_file\n");
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
    if (SOS_DEBUG) printf("reply cap %p\n",(void *) reply_cap);
    elf_load(new_pid, reply_cap, load_args, 0);
    
    if (SOS_DEBUG) printf("start_process_cb2 end\n");
}

void start_process_cb_cont(int pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (TMP_DEBUG) printf("start_process_cb_cont\n");
    if (SOS_DEBUG) printf("reply cap %p\n",(void *) reply_cap);
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

    if (SOS_DEBUG) printf("start_process_cb_cont end\n");
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
    if (SOS_DEBUG) printf("ps done\n");
}

void handle_process_create_cb (int pid, seL4_CPtr reply_cap, void *args, int err) {
    if (SOS_DEBUG) printf("handle_process_create_cb\n");
    if (!err) {
        if (SOS_DEBUG) printf("Process %d\n", pid);
    }
    int child_pid = (int) args;
    if (err) {
        if (SOS_DEBUG) printf("Error, replying on cap %d\n", reply_cap);
        send_seL4_reply(reply_cap, pid, err);
    } else {
        send_seL4_reply(reply_cap, pid, child_pid);
    }  
    if (SOS_DEBUG) printf("handle_process_create_cb ended\n\n\n\n");
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

    //might have triggered a race condition and so we should check if we 
    //have the child
    if (tmp != NULL) {
        cur->next = tmp->next;
        free(tmp);
    }
    if (SOS_DEBUG) printf("remove_child ended\n");
}

void kill_process(int pid, int to_delete, seL4_CPtr reply_cap) {
    //check we have something to delete 
    if (SOS_DEBUG) printf("kill_process cap: %p\n", (void *) reply_cap);
    if (proc_table[to_delete] == NULL) {
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }

    if (to_delete == pid) {
        //deleting ourselves: 
        //-mark ourself as not blocked (yet)
        //-if the parent is waiting on us, set our wait cap to the parent's
        //-remove ourself from our parent
        if (SOS_DEBUG) printf("Deleting self %d\n", pid);
        int parent = proc_table[to_delete]->parent_pid;
        proc_table[to_delete]->status &= ~PROC_BLOCKED;
        if (parent != 0) {
            if (SOS_DEBUG) printf("deleting a process spawned by process %d\n", parent);
            if (proc_table[parent]->wait_pid == to_delete) { 
                if (SOS_DEBUG) printf("our parent was waiting on us.\n");
                proc_table[to_delete]->wait_cap = proc_table[parent]->wait_cap;
            }
            remove_child(parent, to_delete);
        }
    } else if (is_child(pid, to_delete)) {
        //deleting a child:
        //set our wait cap to the reply_cap
        //set our child's wait_cap to the reply_cap
        proc_table[to_delete]->wait_cap = reply_cap;
        if (!proc_table[pid]->wait_cap) {
            proc_table[pid]->wait_cap = reply_cap;
        }
        remove_child(pid, to_delete);
    } else {
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    addr_space *as = proc_table[to_delete];

    as->status |= PROC_DYING;
    
    //increment the wait count in the parent 
    int parent = proc_table[to_delete]->parent_pid;
    if (parent != 0) {
        proc_table[parent]->delete_wait++;
    }
    //kill all its children first 
    child_proc *cur = as->children;
    while (cur != NULL) {
        if (SOS_DEBUG) printf("Killing child %d\n", cur->pid);
        kill_process(pid, cur->pid, reply_cap);
        free(cur);
        cur = cur->next;
    }

    //if child is ready to be killed 
    if (!(as->status & PROC_BLOCKED)) { 

        if (SOS_DEBUG) printf("Calling kill_process_cb\n");
        kill_process_cb(pid, reply_cap, (void *) to_delete, 0);
    }
    if (SOS_DEBUG) printf("kill_process ended\n");
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
    if (SOS_DEBUG) printf("killing: %d, notifying %d\n", to_delete, parent);

    //if we aren't trying to delete ourself and the parent is done waiting on 
    //dying processes, reply to the parent
    if (SOS_DEBUG) printf("cb cap: %p\n", (void *) reply_cap);
    while (parent != 0 
           && (proc_table[parent]->status & PROC_DYING) 
           && (--proc_table[parent]->delete_wait == 0)) {
        current = parent;
        parent = proc_table[parent]->parent_pid;
    } 
    if (SOS_DEBUG) printf("Current %d\n", current);

    seL4_CPtr cap = proc_table[current]->wait_cap;
    if (SOS_DEBUG) printf("cap %p\n", (void *) cap);

    if (SOS_DEBUG) printf("parent %d\n", parent);

    proc_table[current]->wait_cap = 0;

    if (cap != 0) {
        send_seL4_reply(cap, 0, to_delete);
    } else if (parent != 0) {
        seL4_CPtr parent_cap = proc_table[parent]->wait_cap;
        if (parent_cap) {
            send_seL4_reply(parent_cap, 0, to_delete);
        }
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

/*
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
*/

void start_process_load (int parent_pid, seL4_CPtr reply_cap, void *_args) {
    if (SOS_DEBUG) printf("start_process_load\n");
    printf("starting process from pid %d\n", parent_pid);
    start_process_args *args = (start_process_args *) _args;
    char *app_name = args->app_name;
    
    addr_space *parent_as = proc_table[parent_pid];
    //check parent exists 
    if (parent_pid > 0 && parent_as == NULL) {
        //this shouldn't happen
        assert(0);
    }
    args->reply_cap = reply_cap;
    // Check file exists
    int status = nfs_lookup(&mnt_point, app_name, check_elf_nfs_cb, (uintptr_t)args);
        // check if the NFS call succeeded
    if (status != RPC_OK) {
        eprintf("NFS call failed");
        send_seL4_reply(reply_cap, parent_pid, -1);
        return;
    }



    if (SOS_DEBUG) printf("start_process_load ended\n");
}

void check_elf_nfs_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    if (SOS_DEBUG) printf("check_elf_nfs_cb\n");
    start_process_args *args = (start_process_args *) token;
    if (args == NULL) {
        return;
    }
     // Get arguments we need
    seL4_CPtr reply_cap = args->reply_cap;
    int parent_pid = args->parent_pid;
    char *app_name = args->app_name;
    // Check if file was found or anything else went wrong
    if (status != NFS_OK) {
        eprintf("File %s does not exist", app_name);
        send_seL4_reply(reply_cap, parent_pid, -1);
        return;
    }
    // Copy the file handle and attributes to the arguments
    args->elf_fh = malloc(sizeof(fhandle_t));
    args->elf_attr = malloc(sizeof(fattr_t));
    memcpy(args->elf_fh, fh, sizeof(fhandle_t));
    memcpy(args->elf_attr, fattr, sizeof(fattr_t));
    // Set up callback args
    new_as_args *as_args = malloc(sizeof(new_as_args));
    as_args->cb = (callback_ptr) elf_get_frames;
    as_args->cb_args = args;
    new_as(parent_pid, reply_cap, as_args);
    if (SOS_DEBUG) printf("check_elf_nfs_cb ended\n");
}

void elf_get_frames (int pid, seL4_CPtr reply_cap, start_process_args *args, int err) {
    printf("elf_get_frames\n");
    // Set up cb args
    args->curr_elf_frame = 0;
    args->curr_elf_addr  = ELF_LOAD;
    args->max_elf_frames = args->elf_attr->size / PAGE_SIZE;
    int size = args->elf_attr->size;
    // Check for off by one
    if (args->elf_attr->size & ~PAGE_MASK) {
        args->max_elf_frames++;
    }
    int frames = args->max_elf_frames;
    printf("Pid %d Size: %p, Pages: %p\n", pid, (void *) size, (void *) frames);
    send_seL4_reply(reply_cap, pid, 0);
    // Set up table to store elf mapping frames
    args->elf_load_table = malloc(sizeof(int) * args->max_elf_frames);
    // Set up alloc args
    frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
    alloc_args->map     = NOMAP;
    alloc_args->swap    = NOT_SWAPPABLE;
    alloc_args->cb      = (callback_ptr) elf_frame_alloc;
    alloc_args->cb_args = args;
    // Alloc the next frame for elf
    frame_alloc_swap(pid, reply_cap, alloc_args, 0);
    printf("elf_get_frames ended\n");
}


void elf_frame_alloc (int pid, seL4_CPtr reply_cap, frame_alloc_args *alloc_args, int err) {
    if (SOS_DEBUG) printf("elf_frame_alloc\n");
    start_process_args *args = alloc_args->cb_args;
    // Update allocate state
    int *elf_load_table =  args->elf_load_table;
    // Store the frame index for later
    elf_load_table[args->curr_elf_frame] = alloc_args->index;
    
    // Get args
    int curr_frame = args->curr_elf_frame;
    int max_frames = args->max_elf_frames;
    seL4_Word curr_addr = args->curr_elf_addr;
    // Other variables
    int num_frames = curr_frame + 1;
    int index = elf_load_table[curr_frame];
    // Map into kernel contiguously
    int map_err = map_page(frametable[index].frame_cap
                          ,seL4_CapInitThreadPD
                          ,curr_addr
                          ,seL4_AllRights
                          ,seL4_ARM_Default_VMAttributes
                          );
    assert(map_err == 0);
    printf("Allocated frame %d of %d at %p\n", num_frames, max_frames, (void *) curr_addr);
    if (num_frames < max_frames) {
        // Setup frame_alloc args
        memset(alloc_args, 0, sizeof(frame_alloc_args));
        alloc_args->map     = NOMAP;
        alloc_args->swap    = NOT_SWAPPABLE;
        alloc_args->cb      = (callback_ptr) elf_frame_alloc;
        alloc_args->cb_args = args;
        // Go to next frame
        args->curr_elf_frame++;
        args->curr_elf_addr += PAGE_SIZE;
        // Alloc the next frame for elf
        frame_alloc_swap(pid, reply_cap, alloc_args, 0);
    } else {

        free(alloc_args);
        // now we need to copy the elf from NFS
        args->curr_elf_addr = ELF_LOAD;
        args->offset = 0;
        args->child_pid = pid;
        printf("Doing NFS\n");
        printf("args %p\n", args);
        printf("size: %d\n", args->elf_attr->size);
        int status = nfs_read(args->elf_fh, 0, PAGE_SIZE/2, elf_load_nfs_cb, (uintptr_t)args);
        if (status != NFS_OK) {
            // NFS failed, return
            eprintf("Error caught in elf_frame_alloc\n");
            args->cb(pid, reply_cap, args->cb_args, -1);
            cleanup_elf_table(args);
            free(args);
        }
        printf("NFS call succeeded\n");
    }
    if (SOS_DEBUG) printf("elf_frame_alloc end\n");
}

void elf_load_nfs_cb (uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data) {
    if (SOS_DEBUG) printf("elf_load_nfs_cb\n");
    // Get the args struct form the token
    start_process_args *args = (start_process_args *) token;
    // Increment the read state so we're reading from the correct place in the elf file
    args->offset += count;
    
    // Get the arguments we're using
    int offset = args->offset;
    int size =  args->elf_attr->size;
    seL4_Word addr = args->curr_elf_addr;
    fhandle_t *fh = args->elf_fh;
    int pid = args->child_pid;
    seL4_CPtr reply_cap = args->reply_cap;

    assert(count != 0);
    // Check if NFS call failed
    if (status != NFS_OK) {
        // NFS failed, return
        eprintf("Error caught in elf_load_nfs_cb\n");
        args->cb(pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
        free(args);
        return;
    }
    printf("addr: %p\n", (void *) addr);
    // Copy from the temporary NFS buffer to memory
    memcpy((void *) addr, data, count);
    // Check if we are done
    if (offset == size) {
        printf("Finished, pid %d\n", pid);
        // Finished reading from elf
        process_init(pid, reply_cap, args, 0);

    } else {
        printf("Still reading\n");
        // Still reading from elf file
        args->curr_elf_addr += count;
        int to_load = size - offset;
        if (to_load > PAGE_SIZE/2) {
            to_load = PAGE_SIZE/2;
        }
        int rpc_status = nfs_read(fh, offset, to_load, elf_load_nfs_cb, (uintptr_t)args);
        // Check if RPC succeeded
        if (rpc_status != RPC_OK) {
            eprintf("Error caught in elf_load_nfs_cb\n");
            args->cb(pid, reply_cap, args->cb_args, -1);
            free(args);
            return;
        }
    }
    if (SOS_DEBUG) printf("elf_load_nfs_cb ended\n");
}

void process_init(int new_pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (SOS_DEBUG) printf("process_init\n");
    start_process_args *args = (start_process_args *) _args;
    // Get args that we use
    char *app_name = args->app_name;
    if (err) {
        //if the kernel can't create a process, die
        eprintf("Error caught in process_init\n");
        return;
    }
    if (SOS_DEBUG) printf("pid: %d\n", new_pid);
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
            
            assert(args->parent_pid);
            args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
            cleanup_elf_table(args);
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
        cleanup_elf_table(args);
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
        cleanup_elf_table(args);
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
        cleanup_elf_table(args);
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
        cleanup_elf_table(args);
        free(args);
        cleanup_as(new_pid);
        return;
    }

    alloc_args->map = NOMAP;
    alloc_args->swap = NOT_SWAPPABLE;
    alloc_args->cb = process_init_cb;
    alloc_args->cb_args = args;

    /* Get IPC buffer */
    frame_alloc_swap(new_pid, reply_cap, alloc_args, 0);
    if (SOS_DEBUG) printf("start_process_cb1 end\n");
}

void process_init_cb(int new_pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (SOS_DEBUG) printf("process_init_cb\n");
    frame_alloc_args *alloc_args = (frame_alloc_args *) _args;
    start_process_args *args = (start_process_args *) alloc_args->cb_args;
    // Free frame_alloc args
    int index = alloc_args->index;
    free(alloc_args);
    alloc_args = NULL;   
    if (err || index == FRAMETABLE_ERR) {
        eprintf("Error caught in process_init_cb\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
        free(args);
        cleanup_as(new_pid);
        return;
    }
    // Various local state
    // Address space of the new process
    addr_space *as = proc_table[new_pid];
    seL4_CPtr user_ep_cap;
    
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
        eprintf("Error caught in process_init_cb\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
        free(args);
        cleanup_as(new_pid);
        return;
    }
    printf("Setting index %p to don't swap\n", (void *) index);
    frametable[index].frame_status |= FRAME_DONT_SWAP;

    /* Copy the fault endpoint to the user app to enable IPC */
    if (SOS_DEBUG) printf("PID is %d\n", new_pid);
    user_ep_cap = cspace_mint_cap(as->croot
                                 ,cur_cspace
                                 ,args->fault_ep
                                 ,seL4_AllRights 
                                 ,seL4_CapData_Badge_new(new_pid)
                                 );   
    /* Create a new TCB object */
    as->tcb_addr = ut_alloc(seL4_TCBBits);
    if (!as->tcb_addr) {
        eprintf("Error caught in process_init_cb\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
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
        eprintf("Error caught in process_init_cb\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
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
        eprintf("Error caught in process_init_cbn");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
        free(args);
        cleanup_as(new_pid);
        return;
    }
    /* parse the cpio image */
    dprintf(1, "\nStarting \"%s\"...\n", as->command);
    args->elf_base = (void *) ELF_LOAD;
    printf("tried to do elf load\n");
    if (!args->elf_base) {
        eprintf("Error caught in process_init_cb\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
        free(args);
        cleanup_as(new_pid);
        return;
    }
    /* load the elf image */
    elf_load_args *load_args = malloc(sizeof(elf_load_args));
    if (load_args == NULL) {
        eprintf("Error caught in process_init_cb\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
        free(args);
        cleanup_as(new_pid);
        return;
    }
    load_args->elf_file = args->elf_base;
    load_args->curr_header = 0;
    load_args->cb = process_init_cb2;
    load_args->cb_args = args;
    elf_load(new_pid, reply_cap, load_args, 0);
    
    if (SOS_DEBUG) printf("process_init_cb end\n");
}

void process_init_cb2(int pid, seL4_CPtr reply_cap, void *_args, int err) {
    if (SOS_DEBUG) printf("process_init_cb2\n");
    start_process_args *args = (start_process_args *) _args;

    if (err) {
        eprintf("Error caught in process_init_cb2\n");

        assert(args->parent_pid);
        args->cb(args->parent_pid, reply_cap, args->cb_args, -1);
        cleanup_elf_table(args);
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

    free(args->elf_fh);
    free(args->elf_attr);
    cleanup_elf_table(args);
    free(args);

    if (SOS_DEBUG) printf("process_init_cb2 end\n");
}

void cleanup_elf_table(start_process_args *args) {
    int *elf_table = args->elf_load_table;
    int max_frames = args->max_elf_frames;
    int index;
    for (int i = 0; i < max_frames; i++) {
        index = elf_table[i];
        if (index != 0) {
            printf("Cleaning up cptr: %d\n", frametable[index].frame_cap);
            frame_free(index);
        }
    }
    free(elf_table);
}
