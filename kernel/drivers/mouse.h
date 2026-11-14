/* kernel/drivers/mouse.h - mouse PS/2 via o controlador 8042 (porta auxiliar) e IRQ 12. */
#ifndef RUBYOS_DRIVERS_MOUSE_H
#define RUBYOS_DRIVERS_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

struct mouse_state {
    int32_t x;
    int32_t y;
    bool left;
    bool right;
    bool middle;
};

/* Habilita a porta auxiliar do 8042, configura o pacote padrao de 3 bytes e desmascara a
 * IRQ 12. x/y comecam no centro de uma tela width x height e ficam presos as suas bordas. */
void mouse_init(uint32_t screen_width, uint32_t screen_height);

/* Posicao e botoes atuais (atualizados pela IRQ; seguro de ler fora dela). */
void mouse_get_state(struct mouse_state *out);

#endif
