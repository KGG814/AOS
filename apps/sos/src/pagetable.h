#include "frametable.h"
#include "proc.h"
#include <sel4/sel4.h>

#define PAGE_MASK   		0xFFFFF000
#define SWAPPED     		0x80000000
#define SWAP_SLOT_MASK      0x7FFFFFFF
#define GUARD_PAGE_FAULT 	-1
#define UNKNOWN_REGION		-2
#define NULL_DEREF			-3

#define EFAULT				4

#define PT_BOTTOM(x)        (((x) & 0x3FF000) >> 12)
#define PT_TOP(x)           (((x) & 0xFFC00000) >> 22)

#define PRM_BUF             0
#define TMP_BUF             1

typedef void (*callback_ptr)(int, seL4_CPtr, void*);

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
  addr_space *as;
  seL4_Word vaddr;
  int ft_index;
  seL4_ARM_PageDirectory pd;
  seL4_CPtr *frame_cap;
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


seL4_CPtr sos_map_page(int ft_index
                      ,seL4_Word vaddr
                      ,seL4_ARM_PageDirectory pd
                      ,addr_space* as
                      ,int pid
                      );

void sos_map_page_swap(int ft_index
                      ,seL4_Word vaddr
                      ,seL4_ARM_PageDirectory pd
                      ,addr_space* as
                      ,int pid
                      ,seL4_CPtr reply_cap
                      ,callback_ptr cb
                      ,void *cb_args
                      ,seL4_CPtr *frame_cap
                      );

void handle_vm_fault(seL4_Word badge, int pid);
seL4_Word user_to_kernel_ptr(seL4_Word user_ptr, int pid);
int map_if_valid(seL4_Word vaddr, int pid, callback_ptr cb, void* args, seL4_CPtr reply_cap);
int check_region(seL4_Word start, seL4_Word end);

void copy_in(int pid
           ,seL4_CPtr reply_cap
           ,copy_in_args *args
           );

void copy_out(int pid
           ,seL4_CPtr reply_cap
           ,copy_out_args *args
           );

int copy_page (seL4_Word dst
              ,int count
              ,seL4_Word src
              ,int pid
              ,callback_ptr cb
              ,void *cb_args
              ,seL4_CPtr reply_cap
              ,int src_type
              );

void handle_vm_fault_cb(int pid, seL4_CPtr cap, void* args);

