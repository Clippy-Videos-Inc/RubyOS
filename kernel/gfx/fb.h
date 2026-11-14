/* kernel/gfx/fb.h - framebuffer grafico (0.5): um "back buffer" em RAM comum onde tudo e
 * desenhado (pixels, retangulos, texto, icones com transparencia) e so depois copiado de uma
 * vez para o framebuffer linear de verdade (fb_present()), evitando cintilacao e leituras
 * lentas da memoria de video. A fonte de texto usada e a mesma 8x16 que a BIOS/VGA ja carrega
 * no modo texto -- extraida do hardware antes de trocar de modo grafico (ver fb_init()). */
#ifndef RUBYOS_GFX_FB_H
#define RUBYOS_GFX_FB_H

#include <stdint.h>
#include <stdbool.h>
#include "icons.h"

#define FB_GLYPH_W 8u
#define FB_GLYPH_H 16u

/* Cor solida 0x00RRGGBB (sem canal alfa -- usada para fundos e formas opacas). */
#define FB_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* Tenta ligar o modo grafico width x height (32 bits por pixel) via VBE (kernel/drivers/vbe.h)
 * e aloca o back buffer. Extrai a fonte 8x16 do hardware VGA ANTES de pedir o modo grafico
 * (o modo texto precisa estar ativo para ler a fonte da BIOS). Retorna false (sem mexer em
 * nada) se o VBE nao estiver disponivel ou a alocacao do back buffer falhar. */
bool fb_init(uint32_t width, uint32_t height);

uint32_t fb_width(void);
uint32_t fb_height(void);

void fb_clear(uint32_t rgb);
void fb_putpixel(int32_t x, int32_t y, uint32_t rgb);
void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
/* Retangulo vazado (so a borda), com a espessura em pixels indicada. */
void fb_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb, int32_t thickness);
void fb_hline(int32_t x, int32_t y, int32_t w, uint32_t rgb);
void fb_vline(int32_t x, int32_t y, int32_t h, uint32_t rgb);

/* Um caractere/string na fonte 8x16 extraida do hardware. bg == FB_NO_BG pula o fundo
 * (deixa o que ja estava desenhado la por baixo aparecer, usado para rotulos sobre icones). */
#define FB_NO_BG 0xFFFFFFFFu
void fb_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_text(int32_t x, int32_t y, const char *s, uint32_t fg, uint32_t bg);

/* Desenha um icone (icons.h, pixels 0xAARRGGBB) com alfa-blend pixel a pixel. */
void fb_blit_icon(int32_t x, int32_t y, const struct gfx_icon *icon);

/* Copia o back buffer inteiro para o framebuffer linear real. */
void fb_present(void);

#endif
