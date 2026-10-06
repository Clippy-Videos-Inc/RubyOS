/* kernel/drivers/keyboard.h - teclado PS/2 (scancode set 1, layout US) */
#ifndef RUBYOS_DRIVER_KEYBOARD_H
#define RUBYOS_DRIVER_KEYBOARD_H

void keyboard_init(void);

/* Retorna o proximo caractere digitado ou -1 se o buffer estiver vazio. */
int keyboard_poll(void);

#endif
