#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cpio/cpio.h>
#include <elf/elf.h>

#include "ut_manager/ut.h"
#include <sos/vmem_layout.h>
#include <sos.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

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

int start_process(char *app_name, seL4_CPtr fault_ep, int priority) {
    int pid = new_as();
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
    conditional_panic(err, "Unable to map IPC buffer for user app");
    /* Copy the fault endpoint to the user app to enable IPC */
    user_ep_cap = cspace_mint_cap(as->croot,
                                  cur_cspace,
                                  fault_ep,
                                  seL4_AllRights, 
                                  seL4_CapData_Badge_new(pid));
    
    //???
    /* should be the first slot in the space, hack I know */
    assert(user_ep_cap == 1);
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

    


    /* Start the new process */
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(elf_base);
    context.sp = PROCESS_STACK_TOP;
    seL4_TCB_WriteRegisters(as->tcb_cap, 1, 0, 2, &context);
    return pid;
}
