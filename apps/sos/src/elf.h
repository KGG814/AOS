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

int elf_load(char* elf_file, int pid);

#endif /* _LIBOS_ELF_H_ */
