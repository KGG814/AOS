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
    uint32_t id;
    uint32_t pos; //position in heap 
    timestamp_t end;
    timer_callback_t callback;
    void *data;

    //this is 0 if the timer is not a ticking itmer, >0 if it is.
    uint64_t duration; 
};

volatile struct gpt_map *gpt;

static int initialised = NOT_INITIALISED;

//queue of timers  
static struct timer* queue[MAX_TIMERS] = {NULL}; 

//an array of timers, indexed by their id 
//currently gets used to find a free ID 
static struct timer* timers[MAX_IDS + 1] = {NULL}; 

//create a linked list of ids for quick finding of free ids 
static uint32_t head = 1;
static uint32_t tail = MAX_IDS;
static uint32_t free_ids[MAX_IDS + 1] = {};

static unsigned int num_timers = 0;

//heap functions 
static inline void queue_swap(uint32_t pos1, uint32_t pos2);
static inline void heap_up(uint32_t pos);
static inline void heap_down(uint32_t pos);

//removes a timer from the queue at position pos. 
//does not free the timer or its id; the timer's internal pos will be unreliable
static inline struct timer* unqueue(uint32_t pos);

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
    //initialise ids 
    int i = 1; 
    //this sets: first (0th) and last (MAX_IDth) elements of free_ids to 0
    //every other element i to i + 1
    while (i < MAX_IDS) {
        free_ids[i] = i + 1;
        i++;
    }

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
    struct timer *t = timers[id];

    if (initialised == NOT_INITIALISED) {
        return CLOCK_R_UINT;
    }

    if (t == NULL) {
        return CLOCK_R_FAIL;
    }

    uint32_t pos = t->pos; 

    timers[id] = NULL;
    unqueue(pos); 

    free(t);
    
    free_ids[tail] = id;
    tail = id;
    
    //if the removed timer was the current timer, start the next timer
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
        while (queue[0] != NULL && time_stamp() >= queue[0]->end) {
            //remove current timer from heap. does not free id 
            struct timer *t = queue[0];//unqueue(0);

            //perform the callback 
            if (t->callback != NULL) {
                t->callback(t->id, t->data);
            }
            //this call may have stopped the timer. In this case, we just return 
            if (initialised == TIMER_STOPPED) {
                seL4_IRQHandler_Ack(timerCap);
                return CLOCK_R_OK;
            }

            //delete the timer if it isn't a tick, otherwise put it back on the 
            //heap 
            if (t->duration == 0) {
                unqueue(0);
                timers[t->id] = NULL;
                //insert fresh id to end of list 
                free_ids[tail] = t->id;
                tail = t->id;
                free(t);
            } else {
                t->end += (timestamp_t) t->duration; 
                heap_down(0);
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
    for (int i = 0; i < MAX_IDS; i++) {
        if (queue[i] != NULL) {
            free(queue[i]);
        }
        timers[i + 1] = NULL;
        queue[i] = NULL;
        //dprintf(0, "stop_timer: removing timer %d\n", i);
    }
    num_timers = 0;
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

/* deprecated
int timer_status(void) {
    //this assumes that the rollover handling won't happen in the middle of this 
    //function
    return gpt->gptsr;
}
*/
static inline void queue_swap(uint32_t pos1, uint32_t pos2) {
    struct timer* t = queue[pos1];
    queue[pos1] = queue[pos2];
    queue[pos2] = t;
    queue[pos1]->pos = pos1;
    queue[pos2]->pos = pos2;
}

static inline void heap_up(uint32_t pos) {
    uint32_t parent = (pos - 1)/2;
    while (pos > 0 && queue[parent]->end > queue[pos]->end) {
        queue_swap(pos, parent);
        pos = parent;
        parent = (pos - 1)/2;
    }
}

static inline void heap_down(uint32_t pos) {
    int done = 0; 
    uint32_t left = 2*pos + 1;
    uint32_t right = 2*pos + 2;
    while (pos < num_timers && !done) {
        //check if we have any children
        done = 1;
        //only need to first check if we have a left child due to heap 
        //implementation
        if (left < num_timers && queue[left] != NULL) { 
            if (right < num_timers 
                && queue[right] != NULL 
                && queue[right]->end < queue[left]->end 
                && queue[right]->end < queue[pos]->end) 
            {
                //if the right child exists and is the correct thing to swap, 
                //swap it 
                queue_swap(pos, right);
                pos = right;
                done = 0;
            } else if (queue[left]->end < queue[pos]->end) {
                //otherwise check the left child
                queue_swap(pos, left);
                pos = left;
                done = 0; 
            }
        }
        left = 2*pos + 1;
        right = 2*pos + 2;
    }
}

//removes a timer from the queue at position pos. does not free the timer or its id
//its pos will be unreliable
static inline struct timer* unqueue(uint32_t pos) {
    if (pos > MAX_TIMERS) {
        return NULL;
    }

    struct timer* t = queue[pos];
    if (t == NULL) {
        return NULL;
    } 

    //copy last timer into current pos 
    queue[pos] = queue[--num_timers];

    //set old position of moved timer to NULL
    queue[num_timers] = NULL;

    //reheap 
    heap_down(pos);

    return t;
}

static inline uint32_t super_register(uint64_t delay
                                     ,uint64_t duration
                                     ,timer_callback_t callback
                                     ,void *data) {
    timestamp_t cur_time = time_stamp();
    if (num_timers == MAX_TIMERS) {
        return 0;
    }

    struct timer* t = malloc(sizeof(struct timer));
    if (t == NULL) {
        return 0;
    }

    t->end = cur_time + duration + delay;
    t->callback = callback;
    t->data = data;
    t->duration = duration;

    if (head == INVALID_ID) {//no IDs left
        free(t);
        return 0;
    } 
    t->id = head;
    timers[head] = t;
    head = free_ids[t->id]; //set the head of the free ids to the next free id
    free_ids[t->id] = 0; //invalidate the pointer in the free ids 
    t->pos = num_timers;

    //perform heap insertion
    queue[num_timers] = t;
    heap_up(num_timers);
    num_timers++;

    if (t->pos == 0) {
        //set delay value 
        gpt->gptcr1 = LOWER_32(t->end);
        //Turn on channel 1 interrupts 
        gpt->gptir |= BIT(OF1IE);
    }

    return t->id;
}
