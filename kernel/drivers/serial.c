/* kernel/drivers/serial.c - UART 16550 em COM1 (0x3F8), por polling */
#include "drivers/serial.h"
#include "io.h"

#define COM1 0x3F8

static bool present = false;

void serial_init(void)
{
    outb(COM1 + 1, 0x00);    /* sem interrupcoes */
    outb(COM1 + 3, 0x80);    /* DLAB = 1 */
    outb(COM1 + 0, 0x03);    /* divisor 3 -> 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    /* 8 bits, sem paridade, 1 stop */
    outb(COM1 + 2, 0xC7);    /* FIFO habilitada e limpa */
    outb(COM1 + 4, 0x1E);    /* modo loopback para autoteste */
    outb(COM1 + 0, 0xAE);
    if (inb(COM1 + 0) != 0xAE) {
        present = false;     /* sem UART (ex.: hardware sem COM1) */
        return;
    }
    outb(COM1 + 4, 0x0F);    /* operacao normal */
    present = true;
}

bool serial_present(void)
{
    return present;
}

void serial_write_char(char c)
{
    unsigned spins = 0;

    if (!present)
        return;
    while ((inb(COM1 + 5) & 0x20) == 0) {      /* espera o buffer de transmissao esvaziar */
        if (++spins > 100000u)
            return;                            /* nao trava o kernel se a UART emperrar */
    }
    outb(COM1, (uint8_t)c);
}

int serial_poll(void)
{
    if (!present)
        return -1;
    if (inb(COM1 + 5) & 0x01)
        return inb(COM1);
    return -1;
}
