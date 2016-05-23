#include <stdlib.h>
#include <stdint.h>

#include <libopencmsis/core_cm3.h>

#include <systick.h>


volatile uint32_t milliseconds;

typedef struct {
    systick_callback callback;
    uint32_t trigger_ms;
} systick_callback_t;

#define MAX_SYSTICK_CALLBACKS 1
static systick_callback_t callbacks[MAX_SYSTICK_CALLBACKS];


// ****************************************************************************
static systick_callback_t *find_callback(systick_callback cb)
{
    for (size_t i = 0; i < MAX_SYSTICK_CALLBACKS; i++) {
        if (callbacks[i].callback == cb) {
            return &callbacks[i];
        }
    }

    return NULL;
}


// ****************************************************************************
static systick_callback_t *get_emtpy_callback_slot(void)
{
    return find_callback(NULL);
}


// ****************************************************************************
void init_systick(void)
{
    // 24 MHz / 8 => 3000000 counts per second
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);

    // 3000000/3000 = 1000 overflows per second - every 1ms one interrupt
    // SysTick interrupt every N clock pulses: set reload to N-1
    systick_set_reload(2999);

    systick_interrupt_enable();
    systick_counter_enable();
}


// ****************************************************************************
void systick_set_callback(systick_callback cb, uint32_t duration_ms)
{
    systick_callback_t *slot;

    if (duration_ms == 0) {
        duration_ms = 1;
    }

    slot = find_callback(cb);
    if (slot == NULL) {
        slot = get_emtpy_callback_slot();
        if (slot == NULL) {
            // FIXME: output an overflow message here, need to increase MAX_SYSTICK_CALLBACKS
            return;
        }
    }

    cm_disable_interrupts();
    slot->callback = cb;
    slot->trigger_ms = milliseconds + duration_ms;
    cm_enable_interrupts();
}


// ****************************************************************************
void systick_clear_callback(systick_callback cb)
{
    systick_callback_t *slot;

    slot = find_callback(cb);
    if (slot) {
        cm_disable_interrupts();
        slot->callback = NULL;
        cm_enable_interrupts();
    }
}


// ****************************************************************************
void sys_tick_handler(void)
{
    ++milliseconds;

    for (size_t i = 0; i < MAX_SYSTICK_CALLBACKS; i++) {
        if (callbacks[i].callback != NULL  &&
            callbacks[i].trigger_ms == milliseconds ) {

            systick_callback cb = callbacks[i].callback;
            callbacks[i].callback = NULL;
            (*cb)();
        }
    }
}