/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <sel4/sel4.h>
#include <elf/elf.h>
#include <string.h>
#include <assert.h>
#include <cspace/cspace.h>

#include "elf.h"
#include "frametable.h"
#include "proc.h"
#include "pagetable.h"

#include <sos/vmem_layout.h>
#include <ut_manager/ut.h>
#include <mapping.h>

#define verbose 0
#include <sys/debug.h>
#include <sys/panic.h>
#include "debug.h"

/* Minimum of two values. */
#define MIN(a,b) (((a)<(b))?(a):(b))

//#define PAGESIZE              (1 << (seL4_PageBits))
#define PAGEMASK              ((PAGESIZE) - 1)
#define PAGE_ALIGN(addr)      ((addr) & ~(PAGEMASK))
#define IS_PAGESIZE_ALIGNED(addr) !((addr) &  (PAGEMASK))
#define OFST_MASK 0x00000FFF

void load_segment_into_vspace_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args);
void load_segment_into_vspace_cb_continue(int pid, seL4_CPtr reply_cap, load_segment_args *args);
/*
 * Convert ELF permissions into seL4 permissions.
 */
static inline seL4_Word get_sel4_rights_from_elf(unsigned long permissions) {
    seL4_Word result = 0;

    if (permissions & PF_R)
        result |= seL4_CanRead;
    if (permissions & PF_X)
        result |= seL4_CanRead;
    if (permissions & PF_W)
        result |= seL4_CanWrite;

    return result;
}

/*
 * Inject data into the given vspace.
 */
void load_segment_into_vspace(int pid, seL4_CPtr reply_cap, load_segment_args *args) {
    if (SOS_DEBUG) printf("load_segment_into_vspace pid: %d\n", pid);
    /* Overview of ELF segment loading

       dst: destination base virtual address of the segment being loaded
       segment_size: obvious
       
       So the segment range to "load" is [dst, dst + segment_size).

       The content to load is either zeros or the content of the ELF
       file itself, or both.

       The split between file content and zeros is a follows.

       File content: [dst, dst + file_size)
       Zeros:        [dst + file_size, dst + segment_size)

       Note: if file_size == segment_size, there is no zero-filled region.
       Note: if file_size == 0, the whole segment is just zero filled.

       The code below relies on seL4's frame allocator already
       zero-filling a newly allocated frame.

    */  
    // Get the arguments we use
    // Segment size
    unsigned long segment_size  = args->segment_size;
    // ELF file size
    unsigned long file_size     = args->file_size;
    // Destination in user address space to copy to
    unsigned long dst           = args->dst;
    // Position in the current file, which is also used as the loop condition
    unsigned long pos           = args->pos;

    // Address space of the process
    addr_space *as = proc_table[pid];
    // Make sure inputs are valid
    assert(file_size <= segment_size);
    // Check if we have reached end of segment
    if (pos < segment_size) {    
        // If multiple segments reside in a frame, it is possible a frame has already been allocated
        // Page directory and page table indices
        seL4_Word dir_index = PT_TOP(dst);
        seL4_Word page_index = PT_BOTTOM(dst);
        // Check if address is mapped in already
        if (as->page_directory[dir_index] == NULL || as->page_directory[dir_index][page_index] == 0) {
            // Has not been mapped in, allocate a frame for the ELF file to be copied to
            // Set up frame alloc args    
            frame_alloc_args *alloc_args = malloc(sizeof(frame_alloc_args));
            alloc_args->map = KMAP;
            alloc_args->cb = (callback_ptr) load_segment_into_vspace_cb;
            alloc_args->cb_args = args;
            frame_alloc_swap(pid, reply_cap, alloc_args);
        } else {
            // Already been mapped in, can skip frame allocation
            args->index = as->page_directory[dir_index][page_index];
            args->vaddr = index_to_vaddr(args->index);
            load_segment_into_vspace_cb_continue(pid, reply_cap, args);
        }  
    } else {
        // End of segment reeached, callback
        args->cb(pid, reply_cap, args->cb_args);
    }
    if (SOS_DEBUG) printf("load_segment_into_vspace end\n");
}

void load_segment_into_vspace_cb(int pid, seL4_CPtr reply_cap, frame_alloc_args *args) {
    if (SOS_DEBUG) printf("load_segment_into_vspace_cb\n");
    // Get results from frame_alloc call
    load_segment_args *load_args = (load_segment_args *) args->cb_args;
    // Copy results from frame_alloc call to arguments for this call
    load_args->index = args->index;
    load_args->vaddr = args->vaddr;

    // Free frame alloc args
    free(args);
    // Get args we need for this call
    seL4_ARM_PageDirectory dest_as = load_args->dest_as;
    unsigned long dst = load_args->dst;
    int index = load_args->index;
    // Debud print
    if (SOS_DEBUG) printf("index %p\n", (void *)index);
    // 9242_TODO Remove this line and make sure it works, shouldn't need it
    // as sos_map_page should do this already
    seL4_Word vpage  = PAGE_ALIGN(dst);
    // Address space of this process
    addr_space *as = proc_table[pid];
    
    // Do sos_map_page_swap call
    // 9242_TODO test here by using pid for address space etc.
    sos_map_page_swap(index, vpage, dest_as, as, pid, reply_cap,
                      (callback_ptr) load_segment_into_vspace_cb_continue, args);
   if (SOS_DEBUG) printf("load_segment_into_vspace_cb end\n");
}

void load_segment_into_vspace_cb_continue(int pid, seL4_CPtr reply_cap, load_segment_args *args) {
    if (SOS_DEBUG) printf("load_segment_into_vspace_cb_continue\n");
    // Get the arguments we use
    int index                   = args->index;
    seL4_Word vaddr             = args->vaddr;
    unsigned long pos           = args->pos;
    unsigned long file_size     = args->file_size;
    unsigned long dst           = args->dst;
    char *src                   = args->src;
    // Calculate nuber of bytes left to right
    int nbytes = PAGESIZE - (dst & PAGEMASK);
    // Might not be copying to start of page, so add offset from user ptr
    vaddr = vaddr + (dst & OFST_MASK);
    if (pos < file_size){     
        memcpy((void*)vaddr, (void*)src, MIN(nbytes, file_size - args->pos));
    }   
    seL4_ARM_Page_Unify_Instruction(frametable[index].mapping_cap, 0, PAGESIZE);

    args->pos += nbytes;
    args->dst += nbytes;
    args->src += nbytes;
    load_segment_into_vspace(pid, reply_cap, args);
    if (SOS_DEBUG) printf("load_segment_into_vspace_cb_continue end\n");
}

void elf_load(int pid, seL4_CPtr reply_cap, void *_args) {
    if (SOS_DEBUG) printf("elf_load\n");
    elf_load_args *args = (elf_load_args *) _args;
    char *elf_file = args->elf_file;
    int curr_header = args->curr_header;
    addr_space *as = proc_table[pid];
    seL4_ARM_PageDirectory dest_as = as->vroot;
    /* Ensure that the ELF file looks sane. */
    if (elf_checkFile(elf_file)){
        assert(0==1);
        return;
    }
    int num_headers = elf_getNumProgramHeaders(elf_file);
    if (curr_header < num_headers) {
        char *source_addr;
        unsigned long flags, file_size, segment_size, vaddr;

        /* Skip non-loadable segments (such as debugging data). */
        if (elf_getProgramHeaderType(elf_file, curr_header) != PT_LOAD) {
            args->curr_header++;
            elf_load(pid, reply_cap, args);
        }

        /* Fetch information about this segment. */
        source_addr = elf_file + elf_getProgramHeaderOffset(elf_file, curr_header);
        file_size = elf_getProgramHeaderFileSize(elf_file, curr_header);
        segment_size = elf_getProgramHeaderMemorySize(elf_file, curr_header);
        vaddr = elf_getProgramHeaderVaddr(elf_file, curr_header);
        flags = elf_getProgramHeaderFlags(elf_file, curr_header);

        /* Copy it across into the vspace. */
        dprintf(1, " * Loading segment %08x-->%08x\n", (int)vaddr, (int)(vaddr + segment_size));
        // Set up load segment arguments
        args->curr_header++;
        load_segment_args *segment_args = malloc(sizeof(load_segment_args));
        segment_args->src = source_addr;
        segment_args->dst = vaddr;
        segment_args->pos = 0;
        segment_args->dest_as = dest_as;
        segment_args->segment_size = segment_size;
        segment_args->file_size = file_size;
        segment_args->permissions = get_sel4_rights_from_elf(flags) & seL4_AllRights;
        segment_args->cb = elf_load;
        segment_args->cb_args = args;
        load_segment_into_vspace(pid, 0, segment_args);
        
        //conditional_panic(err != 0, "Elf loading failed!\n");
    } else {
        // Do callback
        args->cb(pid, reply_cap, args->cb_args);
    }
    if (SOS_DEBUG) printf("elf_load end\n");
}

