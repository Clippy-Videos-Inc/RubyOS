/* kernel/drivers/timer.c - PIT canal 0 gerando IRQ 0 a TIMER_HZ */
#include "drivers/timer.h"
#include "interrupts.h"
#include "io.h"

#define PIT_BASE_HZ 1193182u

static volatile uint32_t ticks;

static void timer_irq(struct regs *r)
{
    (void)r;
    ticks++;
}

void timer_init(void)
{
    uint32_t divisor = PIT_BASE_HZ / TIMER_HZ;

    outb(0x43, 0x36);                          /* canal 0, lobyte/hibyte, modo 3 */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    irq_install_handler(0, timer_irq);
    irq_unmask(0);
}

uint32_t timer_ticks(void)
{
    return ticks;
}
