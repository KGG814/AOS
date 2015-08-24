#include <sos/vmem_layout.h>
#include <stdint.h>
uintptr_t morecore_base = PROCESS_VMEM_START;

uintptr_t get_morecore_base(void) {
	return morecore_base;
}
void set_morecore_base(uintptr_t new_base) {
	morecore_base = new_base;
}