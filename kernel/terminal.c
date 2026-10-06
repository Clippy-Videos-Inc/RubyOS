/* kernel/terminal.c - terminal VGA texto + kprintf.
 *
 * Toda saida tambem e espelhada na porta serial COM1 (se existir). Isso permite
 * testar o sistema automaticamente com "qemu -serial stdio" e usar o RubyOS
 * por um console serial. */
#include "terminal.h"
#include "util.h"
#include "io.h"
#include "drivers/serial.h"
#include "process.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static size_t row;
static size_t col;
static uint8_t color = (uint8_t)(VGA_LIGHT_GREY | (VGA_BLACK << 4));

/* Captura de saida (redirecionamento da shell). */
static char *cap_buf;
static size_t cap_max;
static size_t cap_len;
static bool cap_truncated;
static int cap_pid;

static uint16_t vga_entry(char c, uint8_t attr)
{
    return (uint16_t)(uint8_t)c | (uint16_t)((uint16_t)attr << 8);
}

static void update_cursor(void)
{
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void)
{
    size_t x;
    size_t y;

    for (y = 1; y < VGA_HEIGHT; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
    for (x = 0; x < VGA_WIDTH; x++)
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', color);
    row = VGA_HEIGHT - 1;
}

static void serial_mirror(char c)
{
    if (c == '\n') {
        serial_write_char('\r');
        serial_write_char('\n');
    } else if (c == '\b') {
        serial_write_char('\b');
        serial_write_char(' ');
        serial_write_char('\b');
    } else {
        serial_write_char(c);
    }
}

void terminal_setcolor(enum vga_color fg, enum vga_color bg)
{
    color = (uint8_t)(fg | (bg << 4));
}

void terminal_clear(void)
{
    size_t i;
    const char *ansi_clear = "\x1b[2J\x1b[H";

    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = vga_entry(' ', color);
    row = 0;
    col = 0;
    update_cursor();
    while (*ansi_clear != '\0')
        serial_write_char(*ansi_clear++);
}

void terminal_init(void)
{
    terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    terminal_clear();
}

bool terminal_capture_begin(char *buf, size_t cap, int owner_pid)
{
    if (cap_buf != NULL || buf == NULL || cap == 0)
        return false;
    cap_len = 0;
    cap_truncated = false;
    cap_max = cap;
    cap_pid = owner_pid;
    cap_buf = buf;
    return true;
}

size_t terminal_capture_end(bool *truncated)
{
    size_t len = cap_len;

    if (truncated != NULL)
        *truncated = cap_truncated;
    cap_buf = NULL;
    cap_len = 0;
    cap_truncated = false;
    return len;
}

void terminal_putc(char c)
{
    if (cap_buf != NULL) {
        const struct process *cur = process_current();
        if (cur != NULL && cur->pid == cap_pid) {
            if (cap_len < cap_max)
                cap_buf[cap_len++] = c;
            else
                cap_truncated = true;
            return;
        }
    }
    serial_mirror(c);

    switch (c) {
    case '\n':
        col = 0;
        row++;
        break;
    case '\r':
        col = 0;
        break;
    case '\t':
        col = (col + 8) & ~(size_t)7;
        break;
    case '\b':
        if (col > 0) {
            col--;
        } else if (row > 0) {
            row--;
            col = VGA_WIDTH - 1;
        }
        vga[row * VGA_WIDTH + col] = vga_entry(' ', color);
        break;
    default:
        vga[row * VGA_WIDTH + col] = vga_entry(c, color);
        col++;
        break;
    }

    if (col >= VGA_WIDTH) {
        col = 0;
        row++;
    }
    if (row >= VGA_HEIGHT)
        scroll();
    update_cursor();
}

void terminal_write(const char *s)
{
    while (*s != '\0')
        terminal_putc(*s++);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt != '\0'; fmt++) {
        char buf[34];
        const char *s = NULL;
        bool zero = false;
        int width = 0;
        int len;

        if (*fmt != '%') {
            terminal_putc(*fmt);
            continue;
        }
        fmt++;
        if (*fmt == '0') {
            zero = true;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's':
            s = va_arg(ap, const char *);
            if (s == NULL)
                s = "(null)";
            break;
        case 'c':
            buf[0] = (char)va_arg(ap, int);
            buf[1] = '\0';
            s = buf;
            break;
        case 'd':
            s = k_itoa(va_arg(ap, int), buf, 10);
            break;
        case 'u':
            s = k_utoa(va_arg(ap, unsigned int), buf, 10);
            break;
        case 'x':
            s = k_utoa(va_arg(ap, unsigned int), buf, 16);
            break;
        case '%':
            terminal_putc('%');
            continue;
        case '\0':
            va_end(ap);
            return;
        default:
            terminal_putc('%');
            terminal_putc(*fmt);
            continue;
        }

        len = (int)k_strlen(s);
        while (width > len) {
            terminal_putc(zero ? '0' : ' ');
            width--;
        }
        terminal_write(s);
    }
    va_end(ap);
}
