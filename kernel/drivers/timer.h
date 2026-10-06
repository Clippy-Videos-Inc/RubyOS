/* kernel/drivers/timer.h - PIT 8253/8254 (canal 0) a 100 Hz */
#ifndef RUBYOS_DRIVER_TIMER_H
#define RUBYOS_DRIVER_TIMER_H

#include <stdint.h>

#define TIMER_HZ 100u

void timer_init(void);
uint32_t timer_ticks(void);

#endif
