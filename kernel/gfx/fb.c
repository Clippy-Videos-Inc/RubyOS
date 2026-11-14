/* kernel/gfx/fb.c - ver fb.h */
#include "fb.h"
#include "../drivers/vbe.h"
#include "../io.h"
#include "../heap.h"

#define FONT_GLYPHS 256u
/* O gerador de caracteres da VGA reserva 32 bytes por glifo (da para fontes de ate 32
 * linhas), mesmo quando a fonte ativa usa so os primeiros 16 (8x16, o caso do modo texto
 * 80x25 padrao): os outros 16 bytes de cada bloco ficam sem uso. Por isso o espacamento
 * entre glifos na memoria (FONT_STRIDE) e o dobro do que de fato e desenhado (FB_GLYPH_H). */
#define FONT_STRIDE 32u
#define FONT_BYTES  (FONT_GLYPHS * FONT_STRIDE)   /* 256 * 32 = 8192 bytes */

static struct vbe_mode g_mode;
static uint32_t *g_back;                 /* back buffer em RAM, g_mode.width * g_mode.height */
static uint8_t g_font[FONT_BYTES];       /* fonte 8x16 extraida do hardware VGA (ver FONT_STRIDE) */
static bool g_ready;

/* ------------------------------------------------------- fonte (extraida do hardware) */

/* Le os 8192 bytes do bloco de fontes que a BIOS/VGA ja carregou no plano 2 da memoria de
 * caracteres (truque classico: reprograma temporariamente o sequenciador e o controlador
 * grafico da VGA para enxergar o plano 2 "cru" em 0xA0000, le, e devolve tudo como estava).
 * So funciona enquanto a placa ainda esta no modo texto 80x25 padrao (antes de fb_init()
 * trocar para o modo grafico via VBE). */
static void vga_font_dump(uint8_t *out)
{
    volatile uint8_t *plane = (volatile uint8_t *)0xA0000;
    uint8_t old_seq2, old_seq4, old_gc4, old_gc5, old_gc6;
    size_t i;

    outb(0x3C4, 0x02); old_seq2 = inb(0x3C5);
    outb(0x3C4, 0x04); old_seq4 = inb(0x3C5);
    outb(0x3CE, 0x04); old_gc4 = inb(0x3CF);
    outb(0x3CE, 0x05); old_gc5 = inb(0x3CF);
    outb(0x3CE, 0x06); old_gc6 = inb(0x3CF);

    outb(0x3C4, 0x02); outb(0x3C5, 0x04);   /* sequenciador: so o plano 2 habilitado p/ escrita */
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);   /* modo sequencial (sem chain-4 nem odd/even)       */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);   /* controlador grafico: ler do plano 2              */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);   /* modo de leitura 0, sem odd/even                   */
    outb(0x3CE, 0x06); outb(0x3CF, 0x00);   /* mapeia 0xA0000-0xAFFFF (64 KiB), sem odd/even     */

    for (i = 0; i < FONT_BYTES; i++)
        out[i] = plane[i];

    outb(0x3C4, 0x02); outb(0x3C5, old_seq2);
    outb(0x3C4, 0x04); outb(0x3C5, old_seq4);
    outb(0x3CE, 0x04); outb(0x3CF, old_gc4);
    outb(0x3CE, 0x05); outb(0x3CF, old_gc5);
    outb(0x3CE, 0x06); outb(0x3CF, old_gc6);
}

/* ------------------------------------------------------------------------- inicializacao */

bool fb_init(uint32_t width, uint32_t height)
{
    size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);

    vga_font_dump(g_font);                       /* precisa do modo texto ainda ativo */

    if (!vbe_set_mode(width, height, &g_mode))
        return false;

    g_back = kmalloc(bytes);
    if (g_back == NULL) {
        g_mode.framebuffer = NULL;                /* fb_present() nao faz nada sem back buffer */
        return false;
    }
    g_ready = true;
    fb_clear(FB_RGB(0, 0, 0));
    return true;
}

uint32_t fb_width(void)  { return g_mode.width; }
uint32_t fb_height(void) { return g_mode.height; }

/* ----------------------------------------------------------------------- primitivas */

void fb_clear(uint32_t rgb)
{
    fb_fill_rect(0, 0, (int32_t)g_mode.width, (int32_t)g_mode.height, rgb);
}

void fb_putpixel(int32_t x, int32_t y, uint32_t rgb)
{
    if (!g_ready || x < 0 || y < 0 || (uint32_t)x >= g_mode.width || (uint32_t)y >= g_mode.height)
        return;
    g_back[(uint32_t)y * g_mode.width + (uint32_t)x] = rgb;
}

void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w;
    int32_t y1 = y + h;
    int32_t px;
    int32_t py;

    if (!g_ready)
        return;
    if (x1 > (int32_t)g_mode.width)
        x1 = (int32_t)g_mode.width;
    if (y1 > (int32_t)g_mode.height)
        y1 = (int32_t)g_mode.height;

    for (py = y0; py < y1; py++) {
        uint32_t *line = &g_back[(uint32_t)py * g_mode.width];
        for (px = x0; px < x1; px++)
            line[px] = rgb;
    }
}

void fb_hline(int32_t x, int32_t y, int32_t w, uint32_t rgb) { fb_fill_rect(x, y, w, 1, rgb); }
void fb_vline(int32_t x, int32_t y, int32_t h, uint32_t rgb) { fb_fill_rect(x, y, 1, h, rgb); }

void fb_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb, int32_t thickness)
{
    int32_t t;

    for (t = 0; t < thickness; t++) {
        fb_hline(x, y + t, w, rgb);
        fb_hline(x, y + h - 1 - t, w, rgb);
        fb_vline(x + t, y, h, rgb);
        fb_vline(x + w - 1 - t, y, h, rgb);
    }
}

/* ------------------------------------------------------------------------------ texto */

void fb_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg)
{
    const uint8_t *glyph = &g_font[(unsigned char)c * FONT_STRIDE];
    uint32_t row;

    if (bg != FB_NO_BG)
        fb_fill_rect(x, y, (int32_t)FB_GLYPH_W, (int32_t)FB_GLYPH_H, bg);
    for (row = 0; row < FB_GLYPH_H; row++) {
        uint8_t bits = glyph[row];
        uint32_t bit;

        for (bit = 0; bit < FB_GLYPH_W; bit++) {
            if ((bits & (0x80u >> bit)) != 0)
                fb_putpixel(x + (int32_t)bit, y + (int32_t)row, fg);
        }
    }
}

void fb_draw_text(int32_t x, int32_t y, const char *s, uint32_t fg, uint32_t bg)
{
    int32_t cx = x;

    for (; *s != '\0'; s++) {
        fb_draw_char(cx, y, *s, fg, bg);
        cx += (int32_t)FB_GLYPH_W;
    }
}

/* ------------------------------------------------------------------------------ icones */

void fb_blit_icon(int32_t x, int32_t y, const struct gfx_icon *icon)
{
    uint32_t iy;

    for (iy = 0; iy < icon->h; iy++) {
        uint32_t ix;

        for (ix = 0; ix < icon->w; ix++) {
            uint32_t px = icon->pixels[iy * icon->w + ix];
            uint32_t a = (px >> 24) & 0xFFu;
            int32_t dx = x + (int32_t)ix;
            int32_t dy = y + (int32_t)iy;
            uint32_t dst;
            uint32_t sr, sg, sb, dr, dg, db;

            if (a == 0)
                continue;
            if (dx < 0 || dy < 0 || (uint32_t)dx >= g_mode.width || (uint32_t)dy >= g_mode.height)
                continue;
            if (a == 255u) {
                fb_putpixel(dx, dy, px & 0x00FFFFFFu);
                continue;
            }
            dst = g_back[(uint32_t)dy * g_mode.width + (uint32_t)dx];
            sr = (px >> 16) & 0xFFu; sg = (px >> 8) & 0xFFu; sb = px & 0xFFu;
            dr = (dst >> 16) & 0xFFu; dg = (dst >> 8) & 0xFFu; db = dst & 0xFFu;
            dr = (sr * a + dr * (255u - a)) / 255u;
            dg = (sg * a + dg * (255u - a)) / 255u;
            db = (sb * a + db * (255u - a)) / 255u;
            fb_putpixel(dx, dy, FB_RGB(dr, dg, db));
        }
    }
}

/* --------------------------------------------------------------------------- apresentacao */

void fb_present(void)
{
    uint32_t total;
    uint32_t i;

    if (!g_ready || g_mode.framebuffer == NULL)
        return;
    total = g_mode.width * g_mode.height;
    for (i = 0; i < total; i++)
        g_mode.framebuffer[i] = g_back[i];
}
