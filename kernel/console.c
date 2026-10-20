/* kernel/console.c - fila de entrada do console e Ctrl+C (ver console.h) */
#include "console.h"
#include "io.h"
#include "process.h"
#include "terminal.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"

#define QUEUE_SIZE 256u

static unsigned char queue[QUEUE_SIZE];
static unsigned head;
static unsigned tail;
static int foreground;
static volatile bool interrupt_pending;

static void enqueue(int c)
{
    unsigned next = (head + 1u) % QUEUE_SIZE;

    if (next == tail)
        return;                                 /* cheia: descarta */
    queue[head] = (unsigned char)c;
    head = next;
}

static void accept(int c)
{
    if (c == CONSOLE_CTRL_C && foreground != 0) {
        if (!interrupt_pending)
            terminal_write("^C\n");
        interrupt_pending = true;
        return;
    }
    enqueue(c);
}

void console_pump(void)
{
    int c;

    while ((c = keyboard_poll()) >= 0)
        accept(c);
    while ((c = serial_poll()) >= 0)
        accept(c);
}

int console_trygetc(void)
{
    int c;

    console_pump();
    if (head == tail)
        return -1;
    c = queue[tail];
    tail = (tail + 1u) % QUEUE_SIZE;
    return c;
}

int console_getc(void)
{
    for (;;) {
        int c = console_trygetc();

        if (c >= 0)
            return c;
        process_yield();
    }
}

void console_flush(void)
{
    console_pump();
    head = tail = 0;
}

void console_set_foreground(int pid)
{
    foreground = pid;
    if (pid == 0)
        interrupt_pending = false;
}

int console_foreground(void)
{
    return foreground;
}

bool console_take_interrupt(void)
{
    bool was = interrupt_pending;

    interrupt_pending = false;
    return was;
}
