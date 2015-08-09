#include <stdlib.h>

#include <clock/clock.h>
#include <bits/limits.h>
#include "../../../apps/sos/src/sys/panic.h"

#define PARENT(x) ((x - 1)/2)

#define LEFT(x) (2*x + 1)
#define RIGHT(x) (2*x + 2)

static uint64_t time_stamp_rollovers = 0; 
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
volatile struct gpt_map *gpt;
static int initialised = NOT_INITIALISED;
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

static inline void queue_swap(uint32_t pos1, uint32_t pos2) {
    struct timer* t = queue[pos1];
    queue[pos1] = queue[pos2];
    queue[pos2] = t;
    queue[pos1]->pos = pos1;
    queue[pos2]->pos = pos2;
}

//removes a timer from the queue at position pos. does not free the timer or its id
//its pos will be unreliable
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
    while (pos < num_timers) {
        //check if we have any children
        if (queue[LEFT(pos)] != NULL) {
            if (queue[RIGHT(pos)] != NULL 
                && queue[RIGHT(pos)]->end < queue[LEFT(pos)]->end 
                && queue[RIGHT(pos)]->end < queue[pos]->end
                ) {
                queue_swap(RIGHT(pos), pos);
                pos = RIGHT(pos);
            } else if (queue[LEFT(pos)]->end < queue[pos]->end) {
                queue_swap(LEFT(pos), pos);
                pos = LEFT(pos); 
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

    gpt = (struct gpt_map *) map_device((void*)GPT1_DEVICE_PADDR, PAGE_SIZE);
    /* Disable the GPT */
    gpt->gptcr = 0;
    gpt->gptsr = GPT_STATUS_REGISTER_CLEAR;
    /* Set all writable GPT_IR fields to zero*/
    gpt->gptcr = 0;
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
    /*
    gTimer.end = time_stamp() + delay;
    gTimer.callback = callback;
    gTimer.data = data;
    // Set delay value
    gpt->gptcr1 = LOWER_32(gTimer.end);
    // Turn on channel 1 interrupts
    gpt->gptir |= BIT(OF1IE);
    */
    if (initialised == NOT_INITIALISED) {
        return 0;
    }

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
    if (i == MAX_IDS) {//no IDs LEFT(pos)
        free(t);
        return 0;
    }
    t->id = i;
    timers[i] = t;
    t->pos = num_timers;

    //perform heap insertion
    queue[num_timers] = t;
    while (t->pos > 0 && queue[PARENT(t->pos)]->end > queue[t->pos]->end) {
        queue[t->pos] = queue[PARENT(t->pos)]; //move parent to t's position in queue  
        queue[PARENT(t->pos)]->pos = t->pos; //change pos of parent to t's
        queue[PARENT(t->pos)] = t; //move t to parent's position in queue 
        t->pos = PARENT(t->pos); //change pos of t to parent's
    }

    num_timers++;

    //TODO need to update current timer if new timer has soonest end 
    if (t->pos == 0) {
        //set delay value 
        gpt->gptcr1 = LOWER_32(t->end);
        //Turn on channel 1 interrupts 
        gpt->gptir |= BIT(OF1IE);
    }

    return t->id;
}

uint32_t register_tic(uint64_t duration, timer_callback_t callback, void *data) {
    timestamp_t cur_time = time_stamp();
    if (num_timers == MAX_TIMERS) {
        return 0;
    }

    struct timer* t = malloc(sizeof(struct timer));
    if (t == NULL) {
        return 0;
    }

    t->end = cur_time + (timestamp_t) duration;
    t->callback = callback;
    t->data = data;
    t->duration = duration;

    int i = 0;
    while (i < MAX_IDS && timers[i] != NULL) {
        i++;
    }
    if (i == MAX_IDS) {//no IDs LEFT(pos)
        free(t);
        return 0;
    }
    t->id = i;
    timers[i] = t;
    t->pos = num_timers;

    //perform heap insertion
    queue[num_timers] = t;
    while (t->pos > 0 && queue[PARENT(t->pos)]->end > queue[t->pos]->end) {
        queue[t->pos] = queue[PARENT(t->pos)]; //move parent to t's position in queue  
        queue[PARENT(t->pos)]->pos = t->pos; //change pos of parent to t's
        queue[PARENT(t->pos)] = t; //move t to parent's position in queue 
        t->pos = PARENT(t->pos); //change pos of t to parent's
    }

    num_timers++;

    //TODO need to update current timer if new timer has soonest end 
    if (t->pos == 0) {
        //set delay value 
        gpt->gptcr1 = LOWER_32(t->end);
        //Turn on channel 1 interrupts 
        gpt->gptir |= BIT(OF1IE);
    }

    return t->id;
}

/*
 * Remove a previously registered callback by its ID
 *    id: Unique ID returned by register_time
 * Returns CLOCK_R_OK iff successful.
 */
int remove_timer(uint32_t id) {
    struct timer *t = timers[id];
    timers[id] = NULL;

    if (t == NULL) {
        return CLOCK_R_FAIL;
    }

    uint32_t pos = t->pos; 

    unqueue(pos); 

    free(t);

    if (pos == 0) {
        gpt->gptcr1 = LOWER_32(queue[0]->end);
        gpt->gptir |= BIT(OF1IE);       
    }
    return CLOCK_R_OK;
}

/*
 * Handle an interrupt message sent to 'interrupt_ep' from start_timer
 *
 * Returns CLOCK_R_OK iff successful
 */
int timer_interrupt(void) {
    volatile uint32_t* status = &gpt->gptsr;
    // Interrupt has happened
    if (*status & BIT(OF1)) {
        timestamp_t cur_time = time_stamp();
        *status |= BIT(OF1);
        while (queue[0] != NULL && cur_time >= queue[0]->end) {
            //remove current timer from heap. does not free id 
            struct timer *t = queue[0];//unqueue(0);
           
            //perform the callback 
            if (t->callback != NULL) {
                (*(t->callback))(t->id, t->data);
            }

            //delete the timer if it isn't a tick, otherwise put it back on the 
            //heap 
            if (t->duration == 0) {
                unqueue(0);
                timers[t->id] = NULL;
                free(t);
            } else {
                t->end = cur_time + (timestamp_t) t->duration;
    
                //perform increase key
                int done = 0; 
                uint32_t pos = 0;
                while (pos < num_timers && !done) {
                    //check if we have any children
                    done = 1;
                    if (queue[LEFT(pos)] != NULL) {
                        if (queue[RIGHT(pos)] != NULL 
                        && queue[RIGHT(pos)]->end < queue[LEFT(pos)]->end 
                        && queue[RIGHT(pos)]->end < queue[pos]->end
                        ) {
                            queue_swap(pos, RIGHT(pos));
                            pos = RIGHT(pos);
                            done = 0;
                        } else if (queue[LEFT(pos)]->end < queue[pos]->end) {
                            queue_swap(pos, LEFT(pos));
                            pos = LEFT(pos);
                            done = 0; 
                        }
                    }
                }
            }
        }
        //start the next timer 
        if (queue[0] != NULL) {
            gpt->gptcr1 = LOWER_32(queue[0]->end);
            gpt->gptir |= BIT(OF1IE);
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
    gpt->gptcr &= ~BIT(EN);
    for (int i = 0; i < MAX_IDS; i++) {
        if (queue[i] != NULL) {
            free(queue[i]);
        }
        timers[i] = NULL;
        queue[i] = NULL;
    }
    num_timers = 0;
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
    return TO_64(time_stamp_rollovers, gpt->gptcnt);
    //return gpt->gptcnt + (time_stamp_rollovers << 32);
}

int timer_status(void) {
    //this assumes that the rollover handling won't happen in the middle of this 
    //function
    return gpt->gptsr;
}
