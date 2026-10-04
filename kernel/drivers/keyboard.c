/* kernel/drivers/keyboard.c - teclado PS/2 via IRQ 1.
 *
 * Suporta letras, numeros, pontuacao US, Shift, Caps Lock, Enter, Backspace,
 * Tab e Espaco. Teclas estendidas (setas, Del, Home...) sao ignoradas na 0.1. */
#include "drivers/keyboard.h"
#include "interrupts.h"
#include "io.h"

#include <stdbool.h>
#include <stdint.h>

#define KB_DATA 0x60
#define KB_STATUS 0x64
#define KB_BUF_SIZE 128

/* Linhas do teclado US, indexadas pelo scancode (set 1) menos o inicio da linha. */
static const char row_num[]   = "1234567890-=";      /* 0x02..0x0D */
static const char row_num_s[] = "!@#$%^&*()_+";
static const char row_q[]     = "qwertyuiop[]";      /* 0x10..0x1B */
static const char row_q_s[]   = "QWERTYUIOP{}";
static const char row_a[]     = "asdfghjkl;'`";      /* 0x1E..0x29 */
static const char row_a_s[]   = "ASDFGHJKL:\"~";
static const char row_z[]     = "\\zxcvbnm,./";      /* 0x2B..0x35 */
static const char row_z_s[]   = "|ZXCVBNM<>?";

static volatile uint8_t buffer[KB_BUF_SIZE];
static volatile uint8_t head;    /* escrito so pela IRQ */
static volatile uint8_t tail;    /* escrito so por keyboard_poll */
static bool shift;
static bool caps;
static bool extended;

static char pick(const char *normal, const char *shifted, uint8_t index)
{
    char c = normal[index];
    bool letter = (c >= 'a' && c <= 'z');

    if (letter)
        return (shift != caps) ? shifted[index] : c;   /* Caps so afeta letras */
    return shift ? shifted[index] : c;
}

static char translate(uint8_t code)
{
    if (code >= 0x02 && code <= 0x0D) return pick(row_num, row_num_s, (uint8_t)(code - 0x02));
    if (code >= 0x10 && code <= 0x1B) return pick(row_q,   row_q_s,   (uint8_t)(code - 0x10));
    if (code >= 0x1E && code <= 0x29) return pick(row_a,   row_a_s,   (uint8_t)(code - 0x1E));
    if (code >= 0x2B && code <= 0x35) return pick(row_z,   row_z_s,   (uint8_t)(code - 0x2B));

    switch (code) {
    case 0x0E: return '\b';
    case 0x0F: return '\t';
    case 0x1C: return '\n';
    case 0x39: return ' ';
    default:   return 0;
    }
}

static void push(char c)
{
    uint8_t next = (uint8_t)((head + 1) % KB_BUF_SIZE);
    if (next == tail)
        return;                    /* buffer cheio: descarta */
    buffer[head] = (uint8_t)c;
    head = next;
}

static void keyboard_irq(struct regs *r)
{
    uint8_t sc = inb(KB_DATA);
    bool release;
    uint8_t code;
    char c;

    (void)r;
    if (sc == 0xE0) {              /* prefixo de tecla estendida */
        extended = true;
        return;
    }
    if (extended) {                /* ignora o byte seguinte (setas etc.) */
        extended = false;
        return;
    }

    release = (sc & 0x80) != 0;
    code = (uint8_t)(sc & 0x7F);

    switch (code) {
    case 0x2A:                     /* Shift esquerdo */
    case 0x36:                     /* Shift direito */
        shift = !release;
        return;
    case 0x3A:                     /* Caps Lock alterna ao pressionar */
        if (!release)
            caps = !caps;
        return;
    default:
        break;
    }

    if (release)
        return;
    c = translate(code);
    if (c != 0)
        push(c);
}

void keyboard_init(void)
{
    while (inb(KB_STATUS) & 0x01)  /* descarta bytes pendentes do controlador */
        (void)inb(KB_DATA);
    irq_install_handler(1, keyboard_irq);
    irq_unmask(1);
}

int keyboard_poll(void)
{
    uint8_t c;

    if (head == tail)
        return -1;
    c = buffer[tail];
    tail = (uint8_t)((tail + 1) % KB_BUF_SIZE);
    return c;
}
