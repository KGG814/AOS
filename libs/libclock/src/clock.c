#include <clock/clock.h>


/* 
 * GPT Registers
 */
#define GPT_PADDR       0x02098000

#define GPT_CR          (char*)(GPT_PADDR + 0x00)
#define GPT_PR          (char*)(GPT_PADDR + 0x04)
#define GPT_SR          (char*)(GPT_PADDR + 0x08)
#define GPT_IR          (char*)(GPT_PADDR + 0x0C)
#define GPT_OCR1    	(char*)(GPT_PADDR + 0x10)
#define GPT_OCR2        (char*)(GPT_PADDR + 0x14)
#define GPT_OCR3    	(char*)(GPT_PADDR + 0x18)
#define GPT_CNT     	(char*)(GPT_PADDR + 0x24)

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

#define PG_CLK          0x00000080

/*
 * GPT_CR Bitmasks
 */

 #define IR_ALL         0x0000003F
/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    interrupt_ep:       A (possibly badged) async endpoint that the driver
                          should use for deliverying interrupts to
 *
 * Returns CLOCK_R_OK iff successful.
 */
int start_timer(seL4_CPtr interrupt_ep) {
		char* gpt = map_device(GPT_ADDR, GPT_SIZE);
        /* Disable the GPT */
        *(gpt + GPT_CR) &= ~EN;
        /* Set all writable GPT_IR fields to zero*/
        *(gpt + GPT_IR) &= ~IR_ALL;
        /* Configure Output mode to disconnected, write zeros in OM3, OM2, OM1 *
        *GPT_CR &= ~OM_ALL;
        /* Disable Input Capture Modes*/ 
        *(gpt + GPT_CR) &= ~IM_ALL;
        /* Change clock source to PG_CLK */
        *(gpt + GPT_CR) &= ~PG_CLK;
        /* Assert SWR bit */
        assert(*GPT_CR & SWR == SWR);
        /* Clear GPT status register (set to clear) */
        *(gpt + GPT_SR) = 0xFFFFFFFF;
        /* Make sure the GPT starts from 0 when we start it */
        *(gpt + GPT_CR) &= ENMOD;
        /* Enable the GPT */
        *(gpt + GPT_CR) &= EN;
        (void*) interrupt_ep;
        return 0;
}

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(void) {
        return *(gpt + GPT_CNT);
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
