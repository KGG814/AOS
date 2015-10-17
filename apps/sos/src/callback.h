#ifndef _CALLBACK_H_
#define _CALLBACK_H_

typedef void (*callback_ptr)(int pid
                            ,seL4_CPtr reply_cap
                            ,void *args
                            ,int err);

#endif /* _CALLBACK_H_ */
