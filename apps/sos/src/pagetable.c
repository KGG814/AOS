seL4_Word** page_directory = 0x30000000;
seL4_Word*  page_tables    = 0x30004000;
seL4_Cptr*  cap_directory  = 0x40000000;
seL4_CPtr   page_directory_cap;
seL4_CPtr   cap_directory_cap;

int page_init(void) {
	seL4_ARM_VMAttributes vm_attr = 0;
	/* Allocate a Page Directory */
	seL4_Word page_directory_paddr = ut_alloc(seL4_PageDirBits);
	err = cspace_ut_retype_addr((seL4_Word)page_directory_paddr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &page_directory_cap);
    err |= map_page(page_directory_cap, 
    	            seL4_CapInitThreadPD, 
    	            page_directory, 
                    seL4_AllRights, 
                    vm_attr);
    conditional_panic(err, "Failed to allocate page directory");
    /* Allocate a cap directory */
    seL4_Word cap_directory_paddr = ut_alloc(seL4_PageDirBits);
	err = cspace_ut_retype_addr((seL4_Word)cap_directory_paddr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &cap_directory_cap);
    err |= map_page(cap_directory_cap, 
    	            seL4_CapInitThreadPD, 
    	            cap_directory, 
                    seL4_AllRights, 
                    vm_attr);
    conditional_panic(err, "Failed to allocate page directory");
}

seL4_Word sos_map_page (seL4_Word vaddr, seL4_CapRights rights, seL4_ARM_VMAttributes attr) {
	seL4_Word dir_index = TOP(vaddr);
	seL4_Word page_index = BOTTOM(vaddr);
	/* Check that the page table exists */
	if (page_directory[dir_index] == NULL) {
		/* Create a page table */
		seL4_Word page_table_paddr = ut_alloc(seL4_PageTableBits);
		seL4_Word page_table_vaddr = (seL4_Word)(page_tables) + (dir_index << seL4_PageTableBits);
		err = cspace_ut_retype_addr(seL4_Word page_table_paddr,
	                                seL4_ARM_PageTableObject,
	                                seL4_PageTableBits,
	                                cur_cspace,
	                                &cap_directory[dir_index]);
		err |= map_page(cap_directory[dir_index],
    	            seL4_CapInitThreadPD,
    	            page_table_vaddr,
                    seL4_AllRights,
                    vm_attr);
	    conditional_panic(err, "Failed to allocate page table");
	    page_directory[dir_index] = page_table_vaddr;
	}
	/* Map into the page table */
	seL4_Word vaddr;
	page_directory[dir_index][page_index] = frame_alloc(&vaddr);
}