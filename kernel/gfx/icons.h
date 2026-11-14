/* kernel/gfx/icons.h - icones embutidos (dados gerados por tools/gen_icons.py a partir dos
 * PNGs em assets/icons/, ver kernel/gfx/icons_data.c). */
#ifndef RUBYOS_GFX_ICONS_H
#define RUBYOS_GFX_ICONS_H

#include <stdint.h>

/* Pixels em 0xAARRGGBB (alfa reto), largura x altura, sem padding entre linhas. */
struct gfx_icon {
    uint32_t w;
    uint32_t h;
    const uint32_t *pixels;
};

extern const struct gfx_icon g_icon_calculator; /* calculadora (atalho: comando "calc") */
extern const struct gfx_icon g_icon_computer;   /* computador (atalho: terminal)        */
extern const struct gfx_icon g_icon_cursor;     /* ponteiro do mouse                    */
extern const struct gfx_icon g_icon_directory;  /* pasta (atalho: comando "files")      */
extern const struct gfx_icon g_icon_ruby_start; /* gema de rubi: botao do Menu Iniciar  */

#endif
