#include <clock/clock.h>
#include <bits/limits.h>
#include "../../../apps/sos/src/sys/panic.h"

/* 
 * GPT Registers
 */
#define GPT_PADDR       0x02098000

#define GPT_CR          (0x00)
#define GPT_PR          (0x04)
#define GPT_SR          (0x08)
#define GPT_IR          (0x0C)
#define GPT_OCR1    	(0x10)
#define GPT_OCR2        (0x14)
#define GPT_OCR3    	(0x18)
#define GPT_CNT     	(0x24)

#define GPT_SIZE		0x28
/*
 * GPT_CR Bitmasks
 */

#define EN              0x00000001
#define ENMOD           0x00000002
#define CLKSRC          0x000001C0
#define FRR             0x00000200
#define OM_ALL          0x1FF00000
#define IM_ALL          0x000F0000
#define SWR             0x00008000

#define PG_CLK          0x00000040

#define OM_1            0x00300000

/*
 * GPT_IR Bitmasks
 */

#define IR_ALL         0x0000003F
#define ROVIE          0x00000020
#define OF1IE          0x00000001

#define PRESCALE       66
#define GPT            13
/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    interrupt_ep:       A (possibly badged) async endpoint that the driver
                          should use for deliverying interrupts to
 *
 * Returns CLOCK_R_OK iff successful.
 */

static uint32_t time_stamp_rollovers = 0; 
static volatile char* gpt;

int start_timer(seL4_CPtr interrupt_ep) {
		gpt = map_device((void*)GPT_PADDR, PAGE_SIZE);
		//
        /* Disable the GPT */
        *((volatile uint32_t*)(gpt + GPT_CR)) &= ~EN;
        /* Set all writable GPT_IR fields to zero*/
        *((volatile uint32_t*)(gpt + GPT_IR)) &= ~IR_ALL;
        /* Configure Output mode to disconnected, write zeros in OM3, OM2, OM1 */
        *((volatile uint32_t*)(gpt + GPT_CR)) &= ~OM_ALL;
        /* Disable Input Capture Modes*/ 
        *((volatile uint32_t*)(gpt + GPT_CR)) &= ~IM_ALL;
        /* Assert SWR bit */
        *((volatile uint32_t*)(gpt + GPT_CR)) |= SWR;
        /* Change clock source to PG_CLK */
        *((volatile uint32_t*)(gpt + GPT_CR)) |= PG_CLK;
        /* Set to free run mode */
        *((volatile uint32_t*)(gpt + GPT_CR)) |= FRR;
        /* Set prescale rate */
        *((volatile uint32_t*)(gpt + GPT_PR)) = 0;
        /* Set interrupt on rollover */
        *((volatile uint32_t*)(gpt + GPT_IR)) |= ROVIE;
        /* Clear GPT status register (set to clear) */
        *((volatile uint32_t*)(gpt + GPT_SR)) |= 0x0000002F;
        /* Make sure the GPT starts from 0 when we start it */
        *((volatile uint32_t*)(gpt + GPT_CR)) |= ENMOD;
        /* Enable the GPT */
        *((volatile uint32_t*)(gpt + GPT_CR)) |= EN;
        //(void*) interrupt_ep;

        /* Interrupt setup */
        seL4_CPtr cap = cspace_irq_control_get_cap(cur_cspace, seL4_CapIRQControl, GPT);
        /* Assign to an end point */
        int err;
        /* Badge the cap so the interrupt handler in syscall loop knows this is a timer interrupt*/
        seL4_CPtr badged_cap = cspace_mint_cap(cur_cspace, cur_cspace, interrupt_ep, seL4_AllRights, seL4_CapData_Badge_new(IRQ_BADGE_TIMER | IRQ_EP_BADGE));
        conditional_panic(!cap, "Failed to acquire and IRQ control cap");
        /* Assign to an end point */
        err = seL4_IRQHandler_SetEndpoint(cap, badged_cap);
        conditional_panic(err, "Failed to set interrupt endpoint");
        /* Ack the handler before continuing */
        err = seL4_IRQHandler_Ack(cap);
        conditional_panic(err, "Failure to acknowledge pending interrupts");
        return *((volatile uint32_t*)(gpt + GPT_IR));
}


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
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int stop_timer(void);

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(void) {
    //this assumes that the rollover handling won't happen in the middle of this 
    //function
    timestamp_t time = *((volatile uint32_t*)(gpt + GPT_CNT));
    return time + (time_stamp_rollovers << 32);
}
/*\
 * Register a callback to be called after a given delay
 *    delay:  Delay time in microseconds before callback is invoked
 *    callback: Function to be called
 *    data: Custom data to be passed to callback function
 *
 * Returns 0 on failure, otherwise an unique ID for this timeout
 */
//uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data)

int timer_status(void) {
    //this assumes that the rollover handling won't happen in the middle of this 
    //function
    return *((volatile uint32_t*)(gpt + GPT_SR));
}
