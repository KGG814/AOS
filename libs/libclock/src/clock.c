#include <stdlib.h>

#include <clock/clock.h>
#include <bits/limits.h>
#include "../../../apps/sos/src/sys/panic.h"

#define NOT_INITIALISED 0
#define INITIALISED 1
#define TIMER_STOPPED 2

#define INVALID_ID 0

#define GPT1_DEVICE_PADDR 0x02098000
#define GPT1_INTERRUPT 87

static uint64_t time_stamp_rollovers = 0; 
seL4_CPtr timerCap;

struct timer {
    timestamp_t end;
    timer_callback_t callback;
    void *data;

    //make timers a doubly linked list.
    struct timer* prev;
    struct timer* next;

    //this is 0 if the timer is not a ticking itmer, >0 if it is.
    uint64_t duration; 
};

struct timer* head = NULL;

volatile struct gpt_map *gpt;

static int initialised = NOT_INITIALISED;

//inline function for performing ordered insert
static inline void insert(struct timer* t);

//master function for creating timers/ ticking timers. 
static inline uint32_t super_register(uint64_t delay
                                     ,uint64_t duration
                                     ,timer_callback_t callback
                                     ,void *data);

/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    interrupt_ep:       A (possibly badged) async endpoint that the driver
 should use for deliverying interrupts to
 *
 * Returns CLOCK_R_OK iff successful.
 */
int start_timer(seL4_CPtr interrupt_ep) {
    //no need to error check; map_device panics on error.
    //TODO: always make sure this is the case
    if (initialised == NOT_INITIALISED) {
        gpt = (struct gpt_map *) map_device((void*)GPT1_DEVICE_PADDR, PAGE_SIZE);
    }
    /* Disable the GPT */
    gpt->gptcr = 0;
    gpt->gptsr = GPT_STATUS_REGISTER_CLEAR;
    /* Set all writable GPT_IR fields to zero*/
    gpt->gptir = 0;
    /* Configure Output mode to disconnected, write zeros in OM3, OM2, OM1 */
    /* Disable Input Capture Modes*/ 
    /* Assert SWR bit */
    gpt->gptcr |= BIT(SWR); /* Reset the GPT */
    /* Change clock source to PG_CLK */
    /* Set to free run mode */
    /* Set prescale rate */

    /* Clear GPT status register (set to clear) */
    /* Make sure the GPT starts from 0 when we start it */
    gpt->gptcr = BIT(FRR) | BIT(CLKSRC) | BIT(ENMOD);
    gpt->gptpr = PRESCALE;
    /* Enable the GPT */
    gpt->gptcr |= BIT(EN);
    /* Set interrupt on rollover */
    gpt->gptir |= BIT(ROVIE);

    //(void*) interrupt_ep;
    
    if (initialised == NOT_INITIALISED) {
        /* Interrupt setup */
        timerCap = cspace_irq_control_get_cap(cur_cspace
                ,seL4_CapIRQControl
                ,GPT1_INTERRUPT);
        /* Assign to an end point */
        int err = seL4_IRQHandler_SetEndpoint(timerCap, interrupt_ep);
        conditional_panic(err, "Failed to set interrupt endpoint");

        /* Ack the handler before continuing */
        err = seL4_IRQHandler_Ack(timerCap);
        conditional_panic(err, "Failure to acknowledge pending interrupts");
        if (err) {
            return CLOCK_R_FAIL;
        }
    }
    initialised = INITIALISED;
    return CLOCK_R_OK;
}

/*
 * Register a callback to be called after a given delay
 *    delay:  Delay time in microseconds before callback is invoked
 *    callback: Function to be called
 *    data: Custom data to be passed to callback function
 *
 * Returns 0 on failure, otherwise an unique ID for this timeout
 */
uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data) {
    return super_register(delay, 0, callback, data);
}

uint32_t register_tic(uint64_t duration, timer_callback_t callback, void *data) {
    return super_register(0, duration, callback, data);
}

/*
 * Remove a previously registered callback by its ID
 *    id: Unique ID returned by register_time
 * Returns CLOCK_R_OK iff successful.
 */
int remove_timer(uint32_t id) {
    if (initialised == NOT_INITIALISED) {
        return CLOCK_R_UINT;
    }

    if ((struct timer*) id == NULL) {
        return CLOCK_R_FAIL;
    }

    //traverse list to see if id is valid
    struct timer *cur = head;
    while (cur != NULL && cur->next != NULL && cur != (struct timer*) id) {
        cur = cur->next;
    }
    //we are either at the head of an empty list, at the end of the list, or 
    //at t itself
    if (cur != (struct timer*) id) {
        return CLOCK_R_FAIL;
    }

    //if the removed timer was the current timer, start the next timer
    if (head == cur) {
        head = cur->next;       
        if (cur->next != NULL) {
            gpt->gptcr1 = LOWER_32(cur->next->end);
            gpt->gptir |= BIT(OF1IE);
        }
    }

    if (cur->prev != NULL) {
        cur->prev->next = cur->next;
    }
    if (cur->next != NULL) {
        cur->next->prev = cur->prev; 
    }
    
    free(cur);
    
    return CLOCK_R_OK;
}

/*
 * Handle an interrupt message sent to 'interrupt_ep' from start_timer
 *
 * Returns CLOCK_R_OK iff successful
 */
int timer_interrupt(void) {
    volatile uint32_t* status = &gpt->gptsr;
    if (initialised != INITIALISED) {
        return CLOCK_R_UINT;
    } 
    //unhandled interrupt
    if (!(*status & BIT(ROV)) && !(*status & BIT(OF1))) {
        return CLOCK_R_FAIL;
    }

    // Interrupt has happened
    if (*status & BIT(OF1)) {
        *status |= BIT(OF1);
        while (head != NULL && time_stamp() >= head->end) {
            //perform the callback 
            if (head->callback != NULL) {
                head->callback((uint32_t) head, head->data);
            }
            //this call may have stopped the timer. In this case, we just return 
            if (initialised == TIMER_STOPPED) {
                seL4_IRQHandler_Ack(timerCap);
                return CLOCK_R_OK;
            }

            //delete the timer if it isn't a tick, otherwise put it back on the 
            //queue 
            struct timer* t = head;
            head = t->next;
            if (head != NULL) {
                head->prev = NULL;
            }

            if (t->duration == 0) {
                free(t);
            } else {
                t->end += (timestamp_t) t->duration; 
                insert(t);
            }
        }
        //start the next timer 
        if (head != NULL) {
            gpt->gptcr1 = LOWER_32(head->end);
            gpt->gptir |= BIT(OF1IE);
        } 
    }   
    // Rollover has occured
    if (*status & BIT(ROV)) {
        time_stamp_rollovers++;
        // Write 1 to clear

        *status |= BIT(ROV);
    }

    seL4_IRQHandler_Ack(timerCap);
    return CLOCK_R_OK;
}

/*
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int stop_timer(void) {
    gpt->gptcr &= ~BIT(EN);
    gpt->gptcr |= BIT(SWR); /* Reset the GPT */
    struct timer* cur = head;
    if (cur != NULL) {
        while (cur->next != NULL) {
            cur = cur->next;
            free(cur->prev);          
        }
        free(cur);
    }
    head = NULL;
    initialised = TIMER_STOPPED;
    time_stamp_rollovers = 0;
    return CLOCK_R_OK;
}

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(void) {
    //this assumes that the rollover handling won't happen in the middle of this 
    //function
    return TO_64(time_stamp_rollovers, gpt->gptcnt);
}

static inline void insert(struct timer* t) {
    if (t == NULL) {
        return;
    }

    if (head == NULL || head->end >= t->end) {
        t->next = head;
        t->prev = NULL; 
        if (head != NULL) {
            head->prev = t;
        }
        head = t;
        return;
    }
    struct timer* cur = head; 
    while (cur->next != NULL && cur->next->end < t->end) {
        cur = cur->next;
    }
    
    t->next = cur->next;
    t->prev = cur;
    cur->next = t;
    if (t->next != NULL) {
        t->next->prev = t;
    }  
}

static inline uint32_t super_register(uint64_t delay
                                     ,uint64_t duration
                                     ,timer_callback_t callback
                                     ,void *data) {
    timestamp_t cur_time = time_stamp();

    struct timer* t = malloc(sizeof(struct timer));
    if (t == NULL) {
        return 0;
    }

    t->end = cur_time + duration + delay;
    t->callback = callback;
    t->data = data;
    t->duration = duration;

    insert(t);
    if (t == head) {
        //set delay value 
        gpt->gptcr1 = LOWER_32(t->end);
        //Turn on channel 1 interrupts 
        gpt->gptir |= BIT(OF1IE);
    }

    return (uint32_t) t;
}
