/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _CLOCK_H_
#define _CLOCK_H_

#include <sel4/sel4.h>
#include <cspace/cspace.h>
#include <mapping.h>
#include <stdio.h>
//#include "../../../apps/sos/src/mapping.h"
/*
 * Return codes for driver functions
 */
#define CLOCK_R_OK     0        /* success */
#define CLOCK_R_UINT (-1)       /* driver not initialised */
#define CLOCK_R_CNCL (-2)       /* operation cancelled (driver stopped) */
#define CLOCK_R_FAIL (-3)       /* operation failed for other reason */

#define IRQ_BADGE_TIMER   (1 << 1)
#define IRQ_BADGE_NETWORK (1 << 0)
#define IRQ_EP_BADGE      (1 << (seL4_BadgeBits - 1))
#define MAX_TIMERS 64
#define MAX_IDS 64

#define ID_FREE 0
#define ID_NOT_FREE 1

typedef uint64_t timestamp_t;
typedef void (*timer_callback_t)(uint32_t id, void *data);


/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    interrupt_ep:       A (possibly badged) async endpoint that the driver
                          should use for deliverying interrupts to
 *
 * Returns CLOCK_R_OK iff successful.
 */
int start_timer(seL4_CPtr interrupt_ep);

/*
 * Register a callback to be called after a given delay
 *    delay:  Delay time in microseconds before callback is invoked
 *    callback: Function to be called
 *    data: Custom data to be passed to callback function
 *
 * Returns 0 on failure, otherwise an unique ID for this timeout
 */
uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data);

/*
 * Remove a previously registered callback by its ID
 *    id: Unique ID returned by register_time
 * Returns CLOCK_R_OK iff successful.
 */
int remove_timer(uint32_t id);

/*
 * Handle an interrupt message sent to 'interrupt_ep' from start_timer
 *
 * Returns CLOCK_R_OK iff successful
 */
int timer_interrupt(void);

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(void);

/*
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int stop_timer(void);

int timer_status(void);

#endif /* _CLOCK_H_ */
