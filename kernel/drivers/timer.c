/* kernel/drivers/timer.c - PIT canal 0 gerando IRQ 0 a TIMER_HZ.
 *
 * A partir da 0.4, cada tick tambem alimenta o escalonador preemptivo
 * (process_on_timer_tick, kernel/process.c): quando a fatia de tempo da tarefa atual
 * acaba, o proprio tick forca uma troca de contexto. */
#include "drivers/timer.h"
#include "interrupts.h"
#include "io.h"
#include "../process.h"

#define PIT_BASE_HZ 1193182u

static volatile uint32_t ticks;

static void timer_irq(struct regs *r)
{
    (void)r;
    ticks++;
    process_on_timer_tick();
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
