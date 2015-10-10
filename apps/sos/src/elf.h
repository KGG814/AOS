/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _LIBOS_ELF_H_
#define _LIBOS_ELF_H_
#include "proc.h"
#include <sel4/sel4.h>



void elf_load(int pid, seL4_CPtr reply_cap, void *_args);

typedef struct _load_segment_args {
	seL4_ARM_PageDirectory dest_as;
	char *src;
	unsigned long dst;
	unsigned long pos;
	unsigned long segment_size;
	unsigned long file_size;
	unsigned long permissions;
	callback_ptr cb;
	void *cb_args;
	// Not initialised
	int index;
	seL4_Word vaddr;
} load_segment_args;

typedef struct _elf_load_args {
	char *elf_file;
	int curr_header;
	callback_ptr cb;
	void *cb_args;
} elf_load_args;

#endif /* _LIBOS_ELF_H_ */
