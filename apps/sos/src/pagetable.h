#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <sel4/sel4.h>

#include "callback.h"
#include "frametable.h"
#include "proc.h"

#define PAGE_MASK   		0xFFFFF000
#define SWAPPED     		0x80000000
#define SWAP_SLOT_MASK      0x7FFFFFFF
#define GUARD_PAGE_FAULT 	-1
#define UNKNOWN_REGION		-2
#define NULL_DEREF			-3
#define PT_ERR              -4

#define EFAULT				4

#define PT_BOTTOM(x)        (((x) & 0x3FF000) >> 12)
#define PT_TOP(x)           (((x) & 0xFFC00000) >> 22)

#define PD_MAX_ENTRIES      (PAGE_SIZE/sizeof(seL4_Word*))
#define PT_MAX_ENTRIES      (PAGE_SIZE/sizeof(seL4_Word))
#define CT_MAX_ENTRIES      (PAGE_SIZE/sizeof(seL4_CPtr))

#define PRM_BUF             0
#define TMP_BUF             1

typedef struct _pt_entry {
  int index;
  seL4_CPtr mapped_cap;
} pt_entry;

typedef struct _vm_init_args {
  callback_ptr cb;
  void *cb_args;
  // Not initialised
  int curr_page;
} vm_init_args;

typedef struct _copy_in_args {
  seL4_Word usr_ptr;
  seL4_Word k_ptr;
  seL4_Word count;
  int nbyte;
  callback_ptr cb;
  seL4_Word cb_arg_1;
  seL4_Word cb_arg_2;
} copy_in_args;

typedef struct _copy_out_args {
  seL4_Word usr_ptr;
  seL4_Word src;
  int nbyte;
  int count;
  callback_ptr cb;
} copy_out_args;

typedef struct  _sos_map_page_args {
  seL4_Word vaddr;
  int ft_index;
  callback_ptr cb;
  void *cb_args;
} sos_map_page_args;

typedef struct _map_if_valid_args {
  int ft_index;
  seL4_Word vaddr;
  callback_ptr cb;
  void *cb_args;
} map_if_valid_args;

typedef struct _copy_page_args {
  seL4_Word dst;
  seL4_Word src;
  int count;
  callback_ptr cb;
  void *cb_args;
  int src_type;
}copy_page_args;

int page_init(int pid);
void pt_cleanup(int pid);

void sos_map_page_swap(int ft_index
                      ,seL4_Word vaddr
                      ,int pid
                      ,seL4_CPtr reply_cap
                      ,callback_ptr cb
                      ,void *cb_args
                      );

void handle_vm_fault(seL4_Word badge, int pid);
seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, int pid);
int map_if_valid(seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap);
int check_region(seL4_Word start, seL4_Word end);
void vm_init(int pid, seL4_CPtr reply_cap, vm_init_args *args);

void copy_in(int pid, seL4_CPtr reply_cap, copy_in_args *args, int err);
void copy_out(int pid, seL4_CPtr reply_cap, copy_out_args *args, int err);

int copy_page(seL4_Word dst
             ,int count
             ,seL4_Word src
             ,int pid
             ,callback_ptr cb
             ,void *cb_args
             ,seL4_CPtr reply_cap
             ,int src_type
             );

void handle_vm_fault_cb(int pid, seL4_CPtr cap, void* args, int err);

#endif /* _PAGETABLE_H_ */
