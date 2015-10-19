/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cpio/cpio.h>
#include <nfs/nfs.h>
#include <elf/elf.h>
#include <serial/serial.h>
#include <clock/clock.h>

#include "pagetable.h"
#include "network.h"
#include "elf.h"
#include "frametable.h"

#include "ut_manager/ut.h"
#include <sos/vmem_layout.h>
#include <sos/sos.h>
#include <autoconf.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>
#include "syscalls.h"
#include "ft_tests.h"
#include "mapping.h"
#include "file_table.h"
#include "vfs.h"
#include "proc.h"
#include "debug.h"

/* This is the index where a clients syscall enpoint will
 * be stored in the clients cspace. */
#define USER_EP_CAP          (1)
/* To differencient between async and and sync IPC, we assign a
 * badge to the a
sync endpoint. The badge that we receive will
 * be the bitwise 'OR' of the async endpoint badge and the badges
 * of all pending notifications. */
#define IRQ_EP_BADGE         (1 << (seL4_BadgeBits - 1))
/* All badged IRQs set high bet, then we use uniq bits to
 * distinguish interrupt sources */
#define IRQ_BADGE_NETWORK (1 << 0)
#define IRQ_BADGE_TIMER   (1 << 1)

#define TTY_NAME             CONFIG_SOS_STARTUP_APP
#define TTY_EP_BADGE         (1)
#define seL4_MsgMaxLength    120

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];
const seL4_BootInfo* _boot_info;

typedef void (* syscall_handler)(seL4_CPtr reply_cap, int pid);
    
syscall_handler syscall_handlers[] = 
{&handle_syscall0
,&handle_sos_write
,&handle_time_stamp
,&handle_brk
,&handle_usleep
,&handle_open
,&handle_close
,&handle_read
,&handle_write
,&handle_getdirent
,&handle_stat
,&handle_my_id
,&handle_process_status
,&handle_process_create
,&handle_process_delete
,&handle_process_wait
};

seL4_CPtr _sos_interrupt_ep_cap;

/**
 * NFS mount point
 */
extern fhandle_t mnt_point;


void handle_syscall(seL4_Word badge, int num_args) {
    seL4_Word syscall_number;
    seL4_CPtr reply_cap;

    syscall_number = seL4_GetMR(0);
    //dprintf(0, "Syscall %d\n", syscall_number); 
    /* Save the caller */
    reply_cap = cspace_save_reply_cap(cur_cspace);
    assert(reply_cap != CSPACE_NULL);
    if (SOS_DEBUG) printf("PID %d doing syscall %d\n", badge, syscall_number);
    if (syscall_number < NUM_SYSCALLS) {
        syscall_handlers[syscall_number](reply_cap, badge);
    } else {
        if (SOS_DEBUG) printf("Unkwown syscall %d.\n", syscall_number);
        send_seL4_reply(reply_cap, badge, -1);
    }
}

void syscall_loop(seL4_CPtr ep) {

    while (1) {
        seL4_Word badge;
        seL4_Word label;
        seL4_MessageInfo_t message;
        message = seL4_Wait(ep, &badge);
        label = seL4_MessageInfo_get_label(message);  
        //dprintf(0, "Badge: %d\n", badge);
        //dprintf(0, "Label: %p\n", label);
        if (badge & IRQ_EP_BADGE) {
            /* Interrupt */
            if (badge & IRQ_BADGE_NETWORK) {  
                printf("Handling NFS\n");
                network_irq();
                printf("NFS handled\n");
            }

            if(badge & IRQ_BADGE_TIMER) {
                //dprintf(0, "Timer Interrupt\n");
                timer_interrupt();
            }

        } else if (proc_table[badge] && proc_table[badge]->status != PROC_DYING) {
            proc_table[badge]->status |= PROC_BLOCKED;
            if (label == seL4_VMFault) {
                /* Page fault */
                //dprintf(0, "vm fault at 0x%08x, pc = 0x%08x, %s\n", seL4_GetMR(1),
                //seL4_GetMR(0),
                //seL4_GetMR(2) ? "Instruction Fault" : "Data fault");
                handle_vm_fault(badge, badge);
            } else if (label == seL4_NoFault) {
                if (SOS_DEBUG) printf("Syscall from process with badge: %d\n", badge);
                /* System call */
                handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1);
            }
        } else {
            printf("Rootserver got an unknown message %p\n", (void *)badge);
        }
    }
}


static void print_bootinfo(const seL4_BootInfo* info) {
    int i;

    /* General info */
    dprintf(1, "Info Page:  %p\n", info);
    dprintf(1,"IPC Buffer: %p\n", info->ipcBuffer);
    dprintf(1,"Node ID: %d (of %d)\n",info->nodeID, info->numNodes);
    dprintf(1,"IOPT levels: %d\n",info->numIOPTLevels);
    dprintf(1,"Init cnode size bits: %d\n", info->initThreadCNodeSizeBits);

    /* Cap details */
    dprintf(1,"\nCap details:\n");
    dprintf(1,"Type              Start      End\n");
    dprintf(1,"Empty             0x%08x 0x%08x\n", info->empty.start, info->empty.end);
    dprintf(1,"Shared frames     0x%08x 0x%08x\n", info->sharedFrames.start, 
                                                   info->sharedFrames.end);
    dprintf(1,"User image frames 0x%08x 0x%08x\n", info->userImageFrames.start, 
                                                   info->userImageFrames.end);
    dprintf(1,"User image PTs    0x%08x 0x%08x\n", info->userImagePTs.start, 
                                                   info->userImagePTs.end);
    dprintf(1,"Untypeds          0x%08x 0x%08x\n", info->untyped.start, info->untyped.end);

    /* Untyped details */
    dprintf(1,"\nUntyped details:\n");
    dprintf(1,"Untyped Slot       Paddr      Bits\n");
    for (i = 0; i < info->untyped.end-info->untyped.start; i++) {
        dprintf(1,"%3d     0x%08x 0x%08x %d\n", i, info->untyped.start + i,
                                                   info->untypedPaddrList[i],
                                                   info->untypedSizeBitsList[i]);
    }

    /* Device untyped details */
    dprintf(1,"\nDevice untyped details:\n");
    dprintf(1,"Untyped Slot       Paddr      Bits\n");
    for (i = 0; i < info->deviceUntyped.end-info->deviceUntyped.start; i++) {
        dprintf(1,"%3d     0x%08x 0x%08x %d\n", i, info->deviceUntyped.start + i,
                                                   info->untypedPaddrList[i + (info->untyped.end - info->untyped.start)],
                                                   info->untypedSizeBitsList[i + (info->untyped.end-info->untyped.start)]);
    }

    dprintf(1,"-----------------------------------------\n\n");

    /* Print cpio data */
    dprintf(1,"Parsing cpio data:\n");
    dprintf(1,"--------------------------------------------------------\n");
    dprintf(1,"| index |        name      |  address   | size (bytes) |\n");
    dprintf(1,"|------------------------------------------------------|\n");
    for(i = 0;; i++) {
        unsigned long size;
        const char *name;
        void *data;

        data = cpio_get_entry(_cpio_archive, i, &name, &size);
        if(data != NULL){
            dprintf(1,"| %3d   | %16s | %p | %12d |\n", i, name, data, size);
        }else{
            break;
        }
    }
    dprintf(1,"--------------------------------------------------------\n");
}

void start_first_process(char* app_name, seL4_CPtr fault_ep) {
    // Set up start_process cb args
    start_process_args *process_args = malloc(sizeof(start_process_args));
    assert(process_args);
    process_args->app_name = app_name;
    process_args->fault_ep = fault_ep;
    process_args->priority = TTY_PRIORITY;
    process_args->cb = handle_process_create_cb;
    process_args->cb_args = NULL;
    process_args->parent_pid = 0;
    start_process_load(0, 0, process_args);
}

static void _sos_ipc_init(seL4_CPtr* ipc_ep, seL4_CPtr* async_ep){
    seL4_Word ep_addr, aep_addr;
    int err;

    /* Create an Async endpoint for interrupts */
    aep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!aep_addr, "No memory for async endpoint");
    err = cspace_ut_retype_addr(aep_addr,
                                seL4_AsyncEndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                async_ep);
    conditional_panic(err, "Failed to allocate c-slot for Interrupt endpoint");

    /* Bind the Async endpoint to our TCB */
    err = seL4_TCB_BindAEP(seL4_CapInitThreadTCB, *async_ep);
    conditional_panic(err, "Failed to bind ASync EP to TCB");


    /* Create an endpoint for user application IPC */
    ep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!ep_addr, "No memory for endpoint");
    err = cspace_ut_retype_addr(ep_addr, 
                                seL4_EndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                ipc_ep);
    conditional_panic(err, "Failed to allocate c-slot for IPC endpoint");
}


static void _sos_init(seL4_CPtr* ipc_ep, seL4_CPtr* async_ep){
    seL4_Word dma_addr;
    seL4_Word low, high;
    int err;

    /* Retrieve boot info from seL4 */
    _boot_info = seL4_GetBootInfo();
    conditional_panic(!_boot_info, "Failed to retrieve boot info\n");
    if(verbose > 0){
        print_bootinfo(_boot_info);
    }

    /* Initialise the untyped sub system and reserve memory for DMA */
    err = ut_table_init(_boot_info);
    conditional_panic(err, "Failed to initialise Untyped Table\n");
    /* DMA uses a large amount of memory that will never be freed */
    dma_addr = ut_steal_mem(DMA_SIZE_BITS);
    conditional_panic(dma_addr == 0, "Failed to reserve DMA memory\n");
    /* find available memory */
    ut_find_memory(&low, &high);

    /* Initialise the untyped memory allocator */
    ut_allocator_init(low, high);
    
    /* Initialise the cspace manager */
    err = cspace_root_task_bootstrap(ut_alloc, ut_free, ut_translate,
                                     malloc, free);
    
    /* Initalise frame table */
    frame_init();
    conditional_panic(err, "Failed to initialise the c space\n");
    /* Reserve frame table memory */
    /* Initialise DMA memory */
    err = dma_init(dma_addr, DMA_SIZE_BITS);
    conditional_panic(err, "Failed to intiialise DMA memory\n");

    /* Initialiase other system compenents here */

    _sos_ipc_init(ipc_ep, async_ep);
}

static inline seL4_CPtr badge_irq_ep(seL4_CPtr ep, seL4_Word badge) {
    seL4_CPtr badged_cap = cspace_mint_cap(cur_cspace, cur_cspace, ep, seL4_AllRights, seL4_CapData_Badge_new(badge | IRQ_EP_BADGE));
    conditional_panic(!badged_cap, "Failed to allocate badged cap");
    return badged_cap;
}

void check(uint32_t id, void* data) {
    //(void *) data;
    dprintf(0, "\nhello %d at time_stamp: %llu\n", id, time_stamp());
}

int count = 0;

void tick_check(uint32_t id, void *data) {
    timestamp_t cur_time = time_stamp();
    dprintf(0, "tick: %12llu (us)\t", id, cur_time);
    count++;
    if (count % 3 == 0) {
        dprintf(0, "\n");
        count = 0;
    }
}

void clock_test(seL4_CPtr interrupt_ep) {
    
    dprintf(0, "registered a timer with id %d\n", register_timer(2000000, &check, NULL));
    
    int id = register_timer(200000000, &check, NULL);
    dprintf(0, "registered a timer with id %d\n", id);
    dprintf(0, "tried to remove timer %d. err: %d\n", id, remove_timer(id));

    dprintf(0, "registered a timer with id %d\n", register_timer(1000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(10000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(10000100, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(10000200, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(9000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(8000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(7000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(6000000, &check, NULL));

    dprintf(0, "tried to remove timer %d. err: %d\n", 5, remove_timer(5));
    dprintf(0, "registered a timer with id %d\n", register_timer(5000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(4000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(3000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(2000000, &check, NULL));
    dprintf(0, "registered a timer with id %d\n", register_timer(1000000, &check, NULL));
    dprintf(0, "tried to remove timer %d. err: %d\n", 5, remove_timer(5));
    dprintf(0, "registered a ticker with id %d\n", register_tic(100000, &tick_check, NULL));

    dprintf(0, "Current us since boot = %d\n", time_stamp());
    
}

/*
 * Main entry point - called by crt.
 */
int main(void) {

    dprintf(0, "\nSpaghetti OS Starting...\n");

    _sos_init(&_sos_ipc_ep_cap, &_sos_interrupt_ep_cap);
    /* Initialise the network hardware */
    network_init(badge_irq_ep(_sos_interrupt_ep_cap, IRQ_BADGE_NETWORK));
    start_timer(badge_irq_ep(_sos_interrupt_ep_cap, IRQ_BADGE_TIMER));

    vfs_init();
    oft_init();
    proc_table_init();
    
    /* Start the user application */
    start_first_process(TTY_NAME, _sos_ipc_ep_cap);
    /* Wait on synchronous endpoint for IPC */
    dprintf(0, "\nSOS entering syscall loop\n");
    //int index = frame_alloc();
    //sos_map_page(index, 0x50000000);
    syscall_loop(_sos_ipc_ep_cap);
    /* Not reached */
    return 0;
}


