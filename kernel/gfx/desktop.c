/* kernel/gfx/desktop.c - ver desktop.h */
#include "desktop.h"
#include "fb.h"
#include "wm.h"
#include "icons.h"
#include "../drivers/mouse.h"
#include "../terminal.h"
#include "../console.h"
#include "../kernel.h"

#define DESKTOP_W 1024u
#define DESKTOP_H 768u

#define TERM_WIN_X 40
#define TERM_WIN_Y 24
#define TERM_WIN_W (8 * TERMINAL_COLS + 16)   /* fonte 8x16: 80 colunas = 640px + 8px de cada lado */
#define TERM_WIN_H (WM_TITLEBAR_H + 16 * TERMINAL_ROWS + 16)

#define INFO_WIN_X 640
#define INFO_WIN_Y 500
#define INFO_WIN_W 340
#define INFO_WIN_H 200

static struct wm_state g_wm;
static bool g_active;
static int g_term_id = -1;

static uint32_t g_last_term_gen = 0xFFFFFFFFu;
static int32_t g_last_mx = -1, g_last_my = -1;
static bool g_last_left;
static bool g_first_tick = true;

/* Paleta classica VGA de 16 cores (mesma ordem de kernel/terminal.h: enum vga_color). */
static const uint32_t g_palette[16] = {
    FB_RGB(0x00, 0x00, 0x00), FB_RGB(0x00, 0x00, 0xAA), FB_RGB(0x00, 0xAA, 0x00), FB_RGB(0x00, 0xAA, 0xAA),
    FB_RGB(0xAA, 0x00, 0x00), FB_RGB(0xAA, 0x00, 0xAA), FB_RGB(0xAA, 0x55, 0x00), FB_RGB(0xAA, 0xAA, 0xAA),
    FB_RGB(0x55, 0x55, 0x55), FB_RGB(0x55, 0x55, 0xFF), FB_RGB(0x55, 0xFF, 0x55), FB_RGB(0x55, 0xFF, 0xFF),
    FB_RGB(0xFF, 0x55, 0x55), FB_RGB(0xFF, 0x55, 0xFF), FB_RGB(0xFF, 0xFF, 0x55), FB_RGB(0xFF, 0xFF, 0xFF)
};

static int find_info_window(void)
{
    int z;

    for (z = 0; z < g_wm.count; z++) {
        int id = wm_window_id_at_z(&g_wm, z);
        const struct wm_window *win = wm_window(&g_wm, id);

        if (win != NULL && win->kind == WM_KIND_INFO)
            return id;
    }
    return -1;
}

static void open_or_focus_info(void)
{
    int id = find_info_window();

    if (id == -1)
        id = wm_add_window(&g_wm, "Sobre", WM_KIND_INFO, true,
                            INFO_WIN_X, INFO_WIN_Y, INFO_WIN_W, INFO_WIN_H);
    else
        wm_raise(&g_wm, id);
}

static void handle_start_item(enum wm_start_item item)
{
    switch (item) {
    case WM_START_TERMINAL:
        wm_raise(&g_wm, g_term_id);
        break;
    case WM_START_CALC:
        wm_raise(&g_wm, g_term_id);
        console_inject_string("calc\n");
        break;
    case WM_START_FILES:
        wm_raise(&g_wm, g_term_id);
        console_inject_string("files\n");
        break;
    case WM_START_SOBRE:
        open_or_focus_info();
        break;
    case WM_START_NONE:
    default:
        break;
    }
}

bool desktop_init(void)
{
    if (!fb_init(DESKTOP_W, DESKTOP_H))
        return false;

    mouse_init(fb_width(), fb_height());
    wm_init(&g_wm, (int32_t)fb_width(), (int32_t)fb_height());
    terminal_set_gfx_backend(true);

    g_term_id = wm_add_window(&g_wm, "Terminal", WM_KIND_TERMINAL, false,
                               TERM_WIN_X, TERM_WIN_Y, TERM_WIN_W, TERM_WIN_H);
    g_active = (g_term_id != -1);
    return g_active;
}

/* ------------------------------------------------------------------------ desenho */

static void draw_close_button(const struct wm_window *win)
{
    int32_t cx = win->x + win->w - WM_CLOSE_MARGIN - WM_CLOSE_SIZE;
    int32_t cy = win->y + (WM_TITLEBAR_H - WM_CLOSE_SIZE) / 2;

    fb_fill_rect(cx, cy, WM_CLOSE_SIZE, WM_CLOSE_SIZE, FB_RGB(0xAA, 0x22, 0x22));
    fb_draw_char(cx, cy - 1, 'x', g_palette[15], FB_NO_BG);
}

static void draw_terminal_body(const struct wm_window *win)
{
    const uint16_t *buf = terminal_screen_buffer();
    int32_t ox = win->x + 8;
    int32_t oy = win->y + WM_TITLEBAR_H + 8;
    size_t cr, cc;
    size_t r, c;

    for (r = 0; r < TERMINAL_ROWS; r++) {
        for (c = 0; c < TERMINAL_COLS; c++) {
            uint16_t entry = buf[r * TERMINAL_COLS + c];
            char ch = (char)(entry & 0xFFu);
            uint8_t attr = (uint8_t)((entry >> 8) & 0xFFu);

            fb_draw_char(ox + (int32_t)c * FB_GLYPH_W, oy + (int32_t)r * FB_GLYPH_H,
                         ch == '\0' ? ' ' : ch, g_palette[attr & 0x0Fu], g_palette[(attr >> 4) & 0x0Fu]);
        }
    }

    terminal_get_cursor(&cr, &cc);
    if (cr < TERMINAL_ROWS && cc < TERMINAL_COLS) {
        /* cursor solido (sem piscar): inverte as cores da celula onde ele esta agora */
        uint16_t entry = buf[cr * TERMINAL_COLS + cc];
        char ch = (char)(entry & 0xFFu);

        fb_draw_char(ox + (int32_t)cc * FB_GLYPH_W, oy + (int32_t)cr * FB_GLYPH_H,
                     ch == '\0' ? ' ' : ch, g_palette[0], g_palette[7]);
    }
}

static void draw_info_body(const struct wm_window *win)
{
    int32_t x = win->x + 12;
    int32_t y = win->y + WM_TITLEBAR_H + 12;

    fb_blit_icon(x, y, &g_icon_computer);
    fb_draw_text(x + 40, y + 2, "RubyOS " RUBYOS_VERSION, g_palette[15], FB_NO_BG);
    fb_draw_text(x + 40, y + 18, "Ruby Powered Operating System", g_palette[7], FB_NO_BG);
    fb_draw_text(x, y + 48, RUBYOS_ARCH, g_palette[7], FB_NO_BG);
    fb_draw_text(x, y + 64, RUNTIME_VERSION, g_palette[7], FB_NO_BG);
    fb_draw_text(x, y + 88, "Interface grafica: VBE (Bochs dispi) + mouse PS/2", g_palette[11], FB_NO_BG);
    fb_draw_text(x, y + 104, "Arraste pela barra de titulo; o X fecha esta janela.", g_palette[11], FB_NO_BG);
}

static void draw_window(int id, bool focused)
{
    const struct wm_window *win = wm_window(&g_wm, id);
    uint32_t title_bg;
    uint32_t body_bg;

    if (win == NULL)
        return;
    title_bg = focused ? FB_RGB(0x20, 0x60, 0xB0) : FB_RGB(0x55, 0x55, 0x5E);
    body_bg = (win->kind == WM_KIND_TERMINAL) ? g_palette[0] : FB_RGB(0xD8, 0xD8, 0xDC);
    fb_fill_rect(win->x, win->y, win->w, WM_TITLEBAR_H, title_bg);
    fb_draw_text(win->x + 6, win->y + 2, win->title, g_palette[15], FB_NO_BG);
    if (win->closable)
        draw_close_button(win);
    fb_fill_rect(win->x, win->y + WM_TITLEBAR_H, win->w, win->h - WM_TITLEBAR_H, body_bg);
    fb_draw_rect(win->x, win->y, win->w, win->h, FB_RGB(0x10, 0x10, 0x14), 1);

    if (win->kind == WM_KIND_TERMINAL)
        draw_terminal_body(win);
    else
        draw_info_body(win);
}

static void draw_taskbar(void)
{
    int32_t tx, ty, tw, th;
    int32_t bx, by, bw, bh;

    wm_taskbar_rect(&g_wm, &tx, &ty, &tw, &th);
    fb_fill_rect(tx, ty, tw, th, FB_RGB(0x2C, 0x2C, 0x34));
    fb_hline(tx, ty, tw, FB_RGB(0x50, 0x50, 0x5C));

    wm_start_button_rect(&g_wm, &bx, &by, &bw, &bh);
    fb_fill_rect(bx, by, bw, bh, g_wm.start_menu_open ? FB_RGB(0x60, 0x18, 0x18) : FB_RGB(0x40, 0x10, 0x10));
    fb_blit_icon(bx + 4, by + (bh - (int32_t)g_icon_ruby_start.h) / 2, &g_icon_ruby_start);
    fb_draw_text(bx + 4 + (int32_t)g_icon_ruby_start.w + 4, by + (bh - (int32_t)FB_GLYPH_H) / 2,
                 "Iniciar", g_palette[15], FB_NO_BG);
}

static void draw_start_item(int index, const struct gfx_icon *icon, const char *label)
{
    int32_t x, y, w, h;

    wm_start_item_rect(&g_wm, index, &x, &y, &w, &h);
    fb_fill_rect(x, y, w, h, FB_RGB(0x3A, 0x3A, 0x44));
    fb_draw_rect(x, y, w, h, FB_RGB(0x20, 0x20, 0x26), 1);
    fb_blit_icon(x + 2, y, icon);
    fb_draw_text(x + 2 + (int32_t)icon->w + 4, y + (h - (int32_t)FB_GLYPH_H) / 2,
                 label, g_palette[15], FB_NO_BG);
}

static void draw_start_menu(void)
{
    draw_start_item(0, &g_icon_computer, "Terminal");
    draw_start_item(1, &g_icon_calculator, "Calculadora");
    draw_start_item(2, &g_icon_directory, "Arquivos");
    draw_start_item(3, &g_icon_ruby_start, "Sobre");
}

static void compose(void)
{
    int focused = wm_focused_id(&g_wm);
    int z;
    struct mouse_state m;

    fb_clear(FB_RGB(0x00, 0x5E, 0x7A));
    for (z = 0; z < g_wm.count; z++) {
        int id = wm_window_id_at_z(&g_wm, z);

        draw_window(id, id == focused);
    }
    draw_taskbar();
    if (g_wm.start_menu_open)
        draw_start_menu();

    mouse_get_state(&m);
    fb_blit_icon(m.x, m.y, &g_icon_cursor);
}

void desktop_tick(void)
{
    struct mouse_state m;
    struct wm_click_result r;
    bool dirty;
    uint32_t term_gen;

    if (!g_active)
        return;

    mouse_get_state(&m);
    r = wm_handle_mouse(&g_wm, m.x, m.y, m.left);

    term_gen = terminal_generation();
    dirty = g_first_tick || r.dirty || m.x != g_last_mx || m.y != g_last_my ||
            m.left != g_last_left || term_gen != g_last_term_gen;

    if (r.start_item != WM_START_NONE)
        handle_start_item(r.start_item);

    g_last_mx = m.x;
    g_last_my = m.y;
    g_last_left = m.left;
    g_last_term_gen = term_gen;
    g_first_tick = false;

    if (!dirty)
        return;
    compose();
    fb_present();
}
