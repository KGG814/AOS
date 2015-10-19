#ifndef _SOS_DEVICES_H_
#define _SOS_DEVICES_H_

#include "vfs.h"
#define CONSOLE_READ_OPEN   1
#define CONSOLE_READ_CLOSE  0

void con_init(void);

vnode *console_open(fmode_t mode, int *err);
vnode *nul_open(fmode_t mode, int *err);

#endif /* _SOS_DEVICES_H_ */
