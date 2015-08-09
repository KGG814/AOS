#include <stdlib.h>

#include <clock/clock.h>
#include <bits/limits.h>
#include "../../../apps/sos/src/sys/panic.h"


/* The timer is prescaled by this value + 1 */
#define PRESCALE       131

static uint64_t time_stamp_rollovers = 0; 
static volatile char* gpt;
seL4_CPtr timerCap;

struct timer {
    uint32_t id;
    uint32_t pos; //position in heap 
    timestamp_t end;
    timer_callback_t callback;
    void *data;

    //this is 0 if the timer is not a tic, >0 if it is.
    uint64_t duration; 
};
volatile struct gpt_map *gpt_registers;
//struct timer *gTimer;
//queue of timers  
static struct timer* queue[MAX_TIMERS] = {NULL}; 
    
//an array of timers, indexed by their id 
//currently gets used to find a free ID 
static struct timer* timers[MAX_IDS] = {NULL}; 

static unsigned int num_timers = 0;

static void delete_timer(struct timer* t) {
    free(t);
}

//removes a timer from the queue at position pos. does not free the timer or its id
static struct timer* unqueue(uint32_t pos) {
    if (pos > MAX_TIMERS) {
        return NULL;
    }
     
    struct timer* t = queue[pos];
    if (t == NULL) {
        return NULL;
    } 

    //copy last timer into current pos 
    queue[pos] = queue[--num_timers];

    //move swapped timer into correct position 
    uint32_t left = 2*pos + 1;
    uint32_t right = 2*pos + 2;
    while (pos < num_timers) {
        //check if we have any children
        if (queue[left] != NULL) {
            if (queue[right] != NULL 
                && queue[right]->end < queue[left]->end 
                && queue[right]->end < queue[pos]->end
                ) {
                queue[pos] = queue[right];
                queue[right] = queue[num_timers];
                queue[pos]->pos = pos; 
                queue[right]->pos = right;
                pos = right;
            } else if (queue[left]->end < queue[pos]->end) {
                queue[pos] = queue[left];
                queue[left] = queue[num_timers];
                queue[pos]->pos = pos; 
                queue[left]->pos = left;
                pos = left; 
            } else {
                pos = num_timers;
            }
        } else {
            pos = num_timers;        
        }
    }

    //set old position of moved timer to NULL
    queue[num_timers] = NULL;
    
    return t;
}

/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    interrupt_ep:       A (possibly badged) async endpoint that the driver
                          should use for deliverying interrupts to
 *
 * Returns CLOCK_R_OK iff successful.
 */

int start_timer(seL4_CPtr interrupt_ep) {

    gpt = map_device((void*)GPT1_DEVICE_PADDR, PAGE_SIZE);
    gpt_registers = (struct gpt_map *)gpt;
    /* Disable the GPT */
    gpt_registers->gptcr = 0;
    gpt_registers->gptsr = GPT_STATUS_REGISTER_CLEAR;
    /* Set all writable GPT_IR fields to zero*/
    gpt_registers->gptcr = 0;
    /* Configure Output mode to disconnected, write zeros in OM3, OM2, OM1 */
    /* Disable Input Capture Modes*/ 
    /* Assert SWR bit */
    gpt_registers->gptcr |= BIT(SWR); /* Reset the GPT */
    /* Change clock source to PG_CLK */
    /* Set to free run mode */
    /* Set prescale rate */
    
    /* Clear GPT status register (set to clear) */
    /* Make sure the GPT starts from 0 when we start it */
    gpt_registers->gptcr = BIT(FRR) | BIT(CLKSRC) | BIT(ENMOD);
    gpt_registers->gptpr = 32;
    /* Enable the GPT */
    gpt_registers->gptcr |= BIT(EN);
    /* Set interrupt on rollover */
    gpt_registers->gptir |= BIT(ROVIE);

    //(void*) interrupt_ep;

    /* Interrupt setup */
    timerCap = cspace_irq_control_get_cap(cur_cspace, seL4_CapIRQControl, GPT1_INTERRUPT);
    /* Assign to an end point */
    int err;
    /* Badge the cap so the interrupt handler in syscall loop knows this is a timer interrupt*/
    /* Assign to an end point */
    err = seL4_IRQHandler_SetEndpoint(timerCap, interrupt_ep);
    conditional_panic(err, "Failed to set interrupt endpoint");
    /* Ack the handler before continuing */
    err = seL4_IRQHandler_Ack(timerCap);
    conditional_panic(err, "Failure to acknowledge pending interrupts");
    return 0;
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
    /*
    gTimer.end = time_stamp() + delay;
    gTimer.callback = callback;
    gTimer.data = data;
    // Set delay value
    gpt_registers->gptcr1 = LOWER_32(gTimer.end);
    // Turn on channel 1 interrupts
    gpt_registers->gptir |= BIT(OF1IE);
    */
    if (num_timers == MAX_TIMERS) {
        return 0;
    }

    struct timer* t = malloc(sizeof(struct timer));
    if (t == NULL) {
        return 0;
    }

    t->end = time_stamp() + (timestamp_t) delay;
    t->callback = callback;
    t->data = data;
    t->duration = 0;

    int i = 0;
    while (i < MAX_IDS && timers[i] != NULL) {
        i++;
    }
    if (i == MAX_IDS) {//no IDs left
        free(t);
        return 0;
    }
    t->id = i;
    timers[i] = t;
    t->pos = num_timers;

    //perform heap insertion
    queue[num_timers] = t;
    while (t->pos > 0 && queue[(t->pos - 1)/2]->end > queue[t->pos]->end) {
        queue[t->pos] = queue[(t->pos - 1)/2]; //move parent to t's position in queue  
        queue[(t->pos - 1)/2]->pos = t->pos; //change pos of parent to t's
        queue[(t->pos - 1)/2] = t; //move t to parent's position in queue 
        t->pos = (t->pos - 1)/2; //change pos of t to parent's
    }

    num_timers++;

    //TODO need to update current timer if new timer has soonest end 
    if (t->pos == 0) {
        //set delay value 
        gpt_registers->gptcr1 = LOWER_32(t->end);
        //Turn on channel 1 interrupts 
        gpt_registers->gptir |= BIT(OF1IE);
    }

    return t->id;
}

uint32_t register_tic(uint64_t duration, timer_callback_t callback, void *data) {
    if (num_timers == MAX_TIMERS) {
        return 0;
    }

    struct timer* t = malloc(sizeof(struct timer));
    if (t == NULL) {
        return 0;
    }

    t->end = time_stamp() + (timestamp_t) duration;
    t->callback = callback;
    t->data = data;
    t->duration = duration;

    int i = 0;
    while (i < MAX_IDS && timers[i] != NULL) {
        i++;
    }
    if (i == MAX_IDS) {//no IDs left
        free(t);
        return 0;
    }
    t->id = i;
    timers[i] = t;
    t->pos = num_timers;

    //perform heap insertion
    queue[num_timers] = t;
    while (t->pos > 0 && queue[(t->pos - 1)/2]->end > queue[t->pos]->end) {
        queue[t->pos] = queue[(t->pos - 1)/2]; //move parent to t's position in queue  
        queue[(t->pos - 1)/2]->pos = t->pos; //change pos of parent to t's
        queue[(t->pos - 1)/2] = t; //move t to parent's position in queue 
        t->pos = (t->pos - 1)/2; //change pos of t to parent's
    }

    num_timers++;

    //TODO need to update current timer if new timer has soonest end 
    if (t->pos == 0) {
        //set delay value 
        gpt_registers->gptcr1 = LOWER_32(t->end);
        //Turn on channel 1 interrupts 
        gpt_registers->gptir |= BIT(OF1IE);
    }

    return t->id;
}

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
int timer_interrupt(void) {
    volatile uint32_t* status = &gpt_registers->gptsr;
    // Interrupt has happened
    if (*status & BIT(OF1)) {
        *status |= BIT(OF1);
        while (queue[0] != NULL &&UPPER_32(time_stamp()) >= UPPER_32(queue[0]->end)) {
            //remove current timer from heap. does not free id 
            struct timer *t = unqueue(0);
           
            //perform the callback 
            if (t->callback != NULL) {
                (*(t->callback))(t->id, t->data);
            }

            //delete the timer if it isn't a tick, otherwise put it back on the 
            //heap 
            if (t->duration == 0) {
                timers[t->id] = NULL;
                free(t);
            } else {
                t->end = time_stamp() + (timestamp_t) t->duration;
                t->pos = num_timers;

                //perform heap insertion
                //should actually just perform a decrease key; TODO change this
                queue[num_timers] = t;
                while (t->pos > 0 && queue[(t->pos - 1)/2]->end > queue[t->pos]->end) {
                    queue[t->pos] = queue[(t->pos - 1)/2]; //move parent to t's position in queue  
                    queue[(t->pos - 1)/2]->pos = t->pos; //change pos of parent to t's
                    queue[(t->pos - 1)/2] = t; //move t to parent's position in queue 
                    t->pos = (t->pos - 1)/2; //change pos of t to parent's
                }

                num_timers++;
            }
        }
        //start the next timer 
        if (queue[0] != NULL) {
            gpt_registers->gptcr1 = LOWER_32(queue[0]->end);
            gpt_registers->gptir |= BIT(OF1IE);
        } 
    }   
    // Rollover has occured
    if (*status & BIT(ROV)) {
        time_stamp_rollovers++;
        // Write 1 to clear
        
        *status |= BIT(ROV);
    // Interupt on channel 1
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
    gpt_registers->gptcr |= BIT(EN);
    return CLOCK_R_OK;
}// {
    //remove all existing timers.
    //return 1;
//}

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(void) {
    //this assumes that the rollover handling won't happen in the middle of this 
    //function
    return TO_64(time_stamp_rollovers, gpt_registers->gptcnt);
    //return gpt_registers->gptcnt + (time_stamp_rollovers << 32);
}

int timer_status(void) {
    //this assumes that the rollover handling won't happen in the middle of this 
    //function
    return gpt_registers->gptsr;
}
