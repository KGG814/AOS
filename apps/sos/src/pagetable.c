seL4_Word page_directory = 0x30000000;
seL4_Word page_tables = 0x30004000;
seL4_CPtr page_directory_cap;

int page_init(void) {
	seL4_Word page_directory_paddr = ut_alloc(seL4_PageDirBits);
	err = cspace_ut_retype_addr(page_directory_paddr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &page_directory_cap);
    conditional_panic(err, "Failed to allocate page directory");

}
