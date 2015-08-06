#include <clock/clock.h>

#include <platsupport/timer.h>
#include <platsupport/plat/timer.h>

#define PRESCALE 		0
#define GPT_STATUS_REGISTER_CLEAR 0x3F
static pstimer_t singleton_timer;
static gpt_t singleton_gpt;
/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    interrupt_ep:       A (possibly badged) async endpoint that the driver
                          should use for deliverying interrupts to
 *
 * Returns CLOCK_R_OK iff successful.


 */
int start_timer(seL4_CPtr interrupt_ep) {
	struct gpt_map gpt_map;

	pstimer_t *timer = &singleton_timer;
    gpt_t *gpt = &singleton_gpt;

    /***/
    timer->properties.upcounter = true;
    timer->properties.timeouts = false;
    timer->properties.bit_width = 32;
    timer->properties.irqs = 1;

    timer->data = (void *) gpt;

    gpt->gpt_map = (volatile struct gpt_map*)&gpt_map;
    gpt->prescaler = PRESCALE;

    /* Disable GPT. */
    /* Sets EN, OM#, IM# to 0 */
    gpt->gpt_map->gptcr = 0;
    /* Set interrupt register to 0 */
    gpt->gpt_map->gptir = 0;
    gpt->gpt_map->gptsr = GPT_STATUS_REGISTER_CLEAR;

    /* Configure GPT. */
    /* Reset to 0 on disable */
    gpt->gpt_map->gptcr |= BIT(ENMOD); 
    /* Reset the GPT */
    gpt->gpt_map->gptcr = 0 | BIT(SWR); 
    gpt->gpt_map->gptcr = BIT(FRR) | BIT(CLKSRC) | BIT(ENMOD); /* GPT can do more but for this just
            set it as free running  so we can tell the time */
    //gpt->gpt_map->gptir = BIT(ROV); /* Interrupt when the timer overflows */
    gpt->gpt_map->gptpr = PRESCALE; /* Set the prescaler */
    gpt->gpt_map->gptcr = BIT(EN);
	return 0;
}

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(void) {
	pstimer_t *timer = &singleton_timer;
    gpt_t *gpt = (gpt_t*) timer->data;
    uint64_t value;

    value = gpt->gpt_map->gptcnt;
    //uint64_t ns = (value / (uint64_t)IPG_FREQ) * NS_IN_US * (gpt->prescaler + 1);
    return value;
}

/*\
 * Register a callback to be called after a given delay
 *    delay:  Delay time in microseconds before callback is invoked
 *    callback: Function to be called
 *    data: Custom data to be passed to callback function
 *
 * Returns 0 on failure, otherwise an unique ID for this timeout
 */
//uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data);

/*
 * Remove a previously registered callback by its ID
 *    id: Unique ID returned by register_time
 * Returns CLOCK_R_OK iff successful.
 */
//int remove_timer(uint32_t id);

/*
 * Handle an interrupt message sent to 'interrupt_ep' from start_timer
 *
 * Returns CLOCK_R_OK iff successful
 */
//int timer_interrupt(void);


/*
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
//int stop_timer(void);