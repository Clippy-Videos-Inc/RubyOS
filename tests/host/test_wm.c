/* tests/host/test_wm.c - testes de kernel/gfx/wm.c no PC hospedeiro (logica pura, sem
 * hardware: so geometria, ordem Z, foco, arrastar e o menu iniciar). */
#include <stdio.h>
#include <stdint.h>

#include "gfx/wm.h"

static int failures;
static int checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void test_add_focus_zorder(void)
{
    struct wm_state wm;
    int a, b, c;

    wm_init(&wm, 800, 600);
    CHECK(wm_focused_id(&wm) == -1);

    a = wm_add_window(&wm, "Terminal", WM_KIND_TERMINAL, false, 10, 10, 300, 200);
    b = wm_add_window(&wm, "Sobre", WM_KIND_INFO, true, 50, 50, 200, 120);
    CHECK(a != -1 && b != -1 && a != b);
    CHECK(wm_focused_id(&wm) == b);                    /* a ultima criada fica na frente */
    CHECK(wm_window_id_at_z(&wm, 0) == a && wm_window_id_at_z(&wm, 1) == b);

    wm_raise(&wm, a);
    CHECK(wm_focused_id(&wm) == a);
    CHECK(wm_window_id_at_z(&wm, 0) == b && wm_window_id_at_z(&wm, 1) == a);

    c = wm_add_window(&wm, "X", WM_KIND_INFO, true, 0, 0, 10, 10);
    CHECK(c != -1);
    wm_close_window(&wm, b);
    CHECK(!wm_window_exists(&wm, b));
    CHECK(wm_window(&wm, b) == NULL);
    CHECK(wm_window_id_at_z(&wm, 0) == a);              /* b sumiu da ordem Z sem deixar buraco */
    CHECK(wm_window_id_at_z(&wm, 1) == c);
    CHECK(wm_window_id_at_z(&wm, 2) == -1);             /* so 2 janelas restam */
}

static void test_window_table_full(void)
{
    struct wm_state wm;
    int ids[WM_MAX_WINDOWS];
    int i;

    wm_init(&wm, 800, 600);
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        ids[i] = wm_add_window(&wm, "W", WM_KIND_INFO, true, 0, 0, 10, 10);
        CHECK(ids[i] != -1);
    }
    CHECK(wm_add_window(&wm, "overflow", WM_KIND_INFO, true, 0, 0, 10, 10) == -1);
    wm_close_window(&wm, ids[3]);
    CHECK(wm_add_window(&wm, "reaproveita", WM_KIND_INFO, true, 0, 0, 10, 10) == ids[3]);
}

static void test_hit_test_and_close_button(void)
{
    struct wm_state wm;
    int id;
    enum wm_hit hit;

    wm_init(&wm, 800, 600);
    id = wm_add_window(&wm, "Sobre", WM_KIND_INFO, true, 100, 100, 200, 120);

    CHECK(wm_hit_test(&wm, 0, 0, &hit) == -1 && hit == WM_HIT_NONE);           /* fora */
    CHECK(wm_hit_test(&wm, 150, 160, &hit) == id && hit == WM_HIT_BODY);       /* corpo */
    CHECK(wm_hit_test(&wm, 150, 105, &hit) == id && hit == WM_HIT_TITLEBAR);   /* barra de titulo */

    /* botao fechar: canto superior direito da barra de titulo (ver WM_CLOSE_*). */
    {
        int32_t cx = 100 + 200 - WM_CLOSE_MARGIN - WM_CLOSE_SIZE / 2;
        int32_t cy = 100 + WM_TITLEBAR_H / 2;

        CHECK(wm_hit_test(&wm, cx, cy, &hit) == id && hit == WM_HIT_CLOSE);
    }

    /* janela sem botao de fechar (o terminal): clicar onde o "x" estaria da' titlebar, nao close. */
    wm_init(&wm, 800, 600);
    id = wm_add_window(&wm, "Terminal", WM_KIND_TERMINAL, false, 100, 100, 200, 120);
    {
        int32_t cx = 100 + 200 - WM_CLOSE_MARGIN - WM_CLOSE_SIZE / 2;
        int32_t cy = 100 + WM_TITLEBAR_H / 2;

        CHECK(wm_hit_test(&wm, cx, cy, &hit) == id && hit == WM_HIT_TITLEBAR);
    }
}

static void test_overlap_topmost_wins(void)
{
    struct wm_state wm;
    int back, front;
    enum wm_hit hit;

    wm_init(&wm, 800, 600);
    back = wm_add_window(&wm, "fundo", WM_KIND_INFO, true, 0, 0, 100, 100);
    front = wm_add_window(&wm, "frente", WM_KIND_INFO, true, 20, 20, 100, 100);
    (void)back;
    CHECK(wm_hit_test(&wm, 50, 50, &hit) == front);     /* sobreposto: a de cima ganha */
}

static void test_drag(void)
{
    struct wm_state wm;
    int id;
    struct wm_click_result r;
    const struct wm_window *win;

    wm_init(&wm, 800, 600);
    id = wm_add_window(&wm, "Terminal", WM_KIND_TERMINAL, false, 100, 100, 200, 120);

    r = wm_handle_mouse(&wm, 150, 105, true);           /* pressiona na barra de titulo */
    CHECK(r.dirty && r.closed_id == -1);
    win = wm_window(&wm, id);
    CHECK(win->x == 100 && win->y == 100);              /* ainda nao moveu, so agarrou */

    r = wm_handle_mouse(&wm, 170, 125, true);           /* arrasta 20,20 */
    CHECK(r.dirty);
    win = wm_window(&wm, id);
    CHECK(win->x == 120 && win->y == 120);

    r = wm_handle_mouse(&wm, 170, 125, false);          /* solta o botao */
    win = wm_window(&wm, id);
    CHECK(win->x == 120 && win->y == 120);              /* para de seguir o mouse */

    r = wm_handle_mouse(&wm, 300, 300, true);           /* mouse se move longe, botao solto */
    win = wm_window(&wm, id);
    CHECK(win->x == 120 && win->y == 120);              /* nao arrasta sem ter clicado na barra */
}

static void test_drag_clamped_to_screen(void)
{
    struct wm_state wm;
    int id;
    const struct wm_window *win;

    wm_init(&wm, 320, 200);
    id = wm_add_window(&wm, "Terminal", WM_KIND_TERMINAL, false, 100, 50, 200, 100);

    wm_handle_mouse(&wm, 150, 55, true);                /* agarra a barra de titulo */
    wm_handle_mouse(&wm, 1000, 1000, true);             /* tenta arrastar para fora da tela */
    win = wm_window(&wm, id);
    CHECK(win->x >= 0 && win->x + win->w <= wm.screen_w);
    CHECK(win->y >= 0 && win->y + win->h <= wm.screen_h - WM_TASKBAR_H);
}

static void test_start_menu(void)
{
    struct wm_state wm;
    int32_t bx, by, bw, bh;
    struct wm_click_result r;

    wm_init(&wm, 800, 600);
    wm_start_button_rect(&wm, &bx, &by, &bw, &bh);
    CHECK(!wm.start_menu_open);

    r = wm_handle_mouse(&wm, bx + 1, by + 1, true);     /* clica no botao Iniciar */
    CHECK(wm.start_menu_open && r.dirty && r.start_item == WM_START_NONE);
    wm_handle_mouse(&wm, bx + 1, by + 1, false);        /* solta (precisa pra zerar prev_left) */

    r = wm_handle_mouse(&wm, bx + 1, by + 1, true);     /* clica de novo: fecha */
    CHECK(!wm.start_menu_open && r.start_item == WM_START_NONE);
    wm_handle_mouse(&wm, bx + 1, by + 1, false);

    wm_handle_mouse(&wm, bx + 1, by + 1, true);         /* abre de novo */
    wm_handle_mouse(&wm, bx + 1, by + 1, false);
    {
        int32_t ix, iy, iw, ih;

        wm_start_item_rect(&wm, 3, &ix, &iy, &iw, &ih); /* item 3 = Sobre */
        r = wm_handle_mouse(&wm, ix + 1, iy + 1, true);
        CHECK(r.start_item == WM_START_SOBRE);
        CHECK(!wm.start_menu_open);                    /* escolher um item fecha o menu */
    }
}

static void test_start_menu_click_outside_closes(void)
{
    struct wm_state wm;
    int32_t bx, by, bw, bh;

    wm_init(&wm, 800, 600);
    wm_start_button_rect(&wm, &bx, &by, &bw, &bh);
    wm_handle_mouse(&wm, bx + 1, by + 1, true);
    wm_handle_mouse(&wm, bx + 1, by + 1, false);
    CHECK(wm.start_menu_open);

    wm_handle_mouse(&wm, 700, 10, true);                /* longe de qualquer item */
    CHECK(!wm.start_menu_open);
}

int main(void)
{
    test_add_focus_zorder();
    test_window_table_full();
    test_hit_test_and_close_button();
    test_overlap_topmost_wins();
    test_drag();
    test_drag_clamped_to_screen();
    test_start_menu();
    test_start_menu_click_outside_closes();

    if (failures == 0)
        printf("wm: %d verificacoes OK\n", checks);
    else
        printf("wm: %d de %d verificacoes falharam\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
