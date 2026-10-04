/* kernel/drivers/serial.h - UART 16550 (COM1) */
#ifndef RUBYOS_DRIVER_SERIAL_H
#define RUBYOS_DRIVER_SERIAL_H

#include <stdbool.h>

/* Inicializa COM1 a 38400 8N1 e testa em loopback; sem UART, fica inativa. */
void serial_init(void);
bool serial_present(void);
void serial_write_char(char c);

/* Retorna o proximo byte recebido ou -1 se nao houver. */
int serial_poll(void);

#endif
