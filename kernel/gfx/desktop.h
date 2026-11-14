/* kernel/gfx/desktop.h - "RubyShell Desktop" (0.5): junta o framebuffer (fb.h), o mouse PS/2
 * (drivers/mouse.h) e o gerenciador de janelas (wm.h) numa area de trabalho com barra de
 * tarefas, Menu Iniciar e duas janelas: o terminal (a shell de sempre, agora dentro de uma
 * janela) e "Sobre". So entra em cena com o parametro de boot gfx=1 (ver kernel.c); sem ele,
 * o sistema continua exatamente como nas versoes anteriores. */
#ifndef RUBYOS_GFX_DESKTOP_H
#define RUBYOS_GFX_DESKTOP_H

#include <stdbool.h>

/* Liga o modo grafico (VBE 1024x768x32), o mouse e o gerenciador de janelas, e cria a janela
 * do terminal. Retorna false (sem mudar nada que ja estivesse funcionando em modo texto) se o
 * VBE/mouse nao estiverem disponiveis. */
bool desktop_init(void);

/* Chamada periodicamente pelo laco ocioso do kernel (a cada volta, varias vezes por segundo):
 * le o mouse, atualiza as janelas e, so se algo mudou, recompoe e apresenta a tela. Nao faz
 * nada se desktop_init() nao tiver sido chamado ou tiver falhado. */
void desktop_tick(void);

#endif
