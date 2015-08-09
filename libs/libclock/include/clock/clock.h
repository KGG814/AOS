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
#include <platsupport/plat/gpt_constants.h>

/* GPT REGISTER BITS */
/* The GPT status register is w1c (write 1 to clear), and there are 6 status bits in the iMX
   status register, so writing the value 0b111111 = 0x3F will clear it. */
#define GPT_STATUS_REGISTER_CLEAR 0x3F

/* Get the upper bits 32 bits of a 64 bit value */
#define UPPER_32(x) ((x) >> 32)

/* Get the lower bits 32 bits of a 64 bit value */
#define LOWER_32(x) ((x) & 0x00000000FFFFFFFF)

/* Create a 64 bit value form 2 32 bit numbers */
#define TO_64(x,y) (((x) << 32) + (y))

/* GPT CONTROL REGISTER BITS */
typedef enum {
    /*
     * This bit enables the GPT.
     */
    EN = 0,

    /*
     * By setting this bit, then when EPIT is disabled (EN=0), then
     * both Main Counter and Prescaler Counter freeze their count at
     * current count values.
     */
    ENMOD = 1,

    /*
     * This read/write control bit enables the operation of the GPT
     *  during debug mode
     */
    DBGEN = 2,

    /*
     *  This read/write control bit enables the operation of the GPT
     *  during wait mode
     */
    WAITEN = 3,

    /*
     * This read/write control bit enables the operation of the GPT
     *  during doze mode
     */
    DOZEN = 4,

    /*
     * This read/write control bit enables the operation of the GPT
     *  during stop mode
     */
    STOPEN = 5,

    /*
     * bits 6-8 -  These bits selects the clock source for the
     *  prescaler and subsequently be used to run the GPT counter.
     */
    CLKSRC = 6,

    /*
     * Freerun or Restart mode.
     *
     * 0 Restart mode
     * 1 Freerun mode
     */
    FRR = 9,

    /*
     * Software reset.
     *
     * This bit is set when the module is in reset state and is cleared
     * when the reset procedure is over. Writing a 1 to this bit
     * produces a single wait state write cycle. Setting this bit
     * resets all the registers to their default reset values except
     * for the EN, ENMOD, STOPEN, DOZEN, WAITEN and DBGEN bits in this
     *  control register.
     */
    SWR = 15,

    /* Input capture channel operating modes */
    IM1 = 16, IM2 = 18,

    /* Output compare channel operating modes */
    OM1 = 20, OM2 = 23, OM3 = 26,

    /* Force output compare channel bits */
    FO1 = 29, FO2 = 30, FO3 = 31

} gpt_control_reg;

/* GPT STATUS REGISTER BITS */
typedef enum {
	/*
	 * Output Compare flags
	 * 
	 * These are set when an event happens on the Output Compare channels
	 */
	OF1 = 0, OF2 = 1, OF3 = 2,

	/*
	 * Input Capture flags
	 *
	 * These are sent when an event happens on the Input Capture channels
	 */
	IF1 = 3, IF2 = 4,

	/*
	 * Rollover flag
	 *
	 * Set when a rollover event occurs
	 */
	ROV = 5

} gpt_status_register;

/* GPT INTERRUPT REGISTER BITS */
typedef enum {
	/*
	 * Output Compare Interrupt Enable 
	 *
	 * Controls interrupts on the respective Output Compare channels
	 */
	OF1IE = 0, OF2IE = 1, OF3IE = 2,

	/* Input Capture Interrupt Enable
	 *
	 * Controls interrupts on the respective Input Compare channels
	 */
	IF1IE = 3, IF12E = 4,

	/* 
	 * Rollover Interrupt Enable
	 * 
	 * Controls the rollover interrupt
	 */
	ROVIE = 5
} gpt_interrupt_register;

/* Memory map for GPT. */
struct gpt_map {
    /* gpt control register */
    uint32_t gptcr;
    /* gpt prescaler register */
    uint32_t gptpr;
    /* gpt status register */
    uint32_t gptsr;
    /* gpt interrupt register */
    uint32_t gptir;
    /* gpt output compare register 1 */
    uint32_t gptcr1;
    /* gpt output compare register 2 */
    uint32_t gptcr2;
    /* gpt output compare register 3 */
    uint32_t gptcr3;
    /* gpt input capture register 1 */
    uint32_t gpticr1;
    /* gpt input capture register 2 */
    uint32_t gpticr2;
    /* gpt counter register */
    uint32_t gptcnt;
};

/*
 * Return codes for driver functions
 */
#define CLOCK_R_OK     0        /* success */
#define CLOCK_R_UINT (-1)       /* driver not initialised */
#define CLOCK_R_CNCL (-2)       /* operation cancelled (driver stopped) */
#define CLOCK_R_FAIL (-3)       /* operation failed for other reason */

#define MAX_TIMERS 64 
#define MAX_IDS 64

/* The timer is prescaled by this value + 1 */
#define PRESCALE 63 

#define NOT_INITIALISED 0
#define INITIALISED 1

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

uint32_t register_tic(uint64_t duration, timer_callback_t callback, void *data);

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
