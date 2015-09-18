#ifndef _SOS_DEVICES_H_
#define _SOS_DEVICES_H_

#include "vfs.h"

void con_init(void);

vnode *console_open(fmode_t mode, int *err);
vnode *nul_open(fmode_t mode, int *err);

#endif /* _SOS_DEVICES_H_ */
