/* kernel/gfx/wm.h - gerenciador de janelas (0.5): so a logica (geometria, ordem Z, foco,
 * arrastar, o menu iniciar) -- NENHUMA chamada de E/S ou de hardware aqui, de proposito, para
 * que de para testar no PC hospedeiro (ver tests/host/test_wm.c) do mesmo jeito que o ramfs,
 * o heap e o pmm. Quem desenha de verdade (fb.h) e quem le o mouse/teclado de verdade
 * (drivers/mouse.h, console.h) fica em kernel/gfx/desktop.c. */
#ifndef RUBYOS_GFX_WM_H
#define RUBYOS_GFX_WM_H

#include <stdint.h>
#include <stdbool.h>

#define WM_MAX_WINDOWS  8
#define WM_TITLE_MAX    32

#define WM_TITLEBAR_H   20
#define WM_CLOSE_SIZE   14
#define WM_CLOSE_MARGIN 3

#define WM_TASKBAR_H        34
#define WM_START_BTN_W      96
#define WM_START_ITEM_H      32
#define WM_START_ITEM_W    150
#define WM_START_ITEM_COUNT  4

enum wm_kind {
    WM_KIND_TERMINAL = 0,   /* a shell existente (RubyShell ou shell C), ver terminal.h */
    WM_KIND_INFO     = 1    /* janela "Sobre": texto estatico com a versao do sistema */
};

struct wm_window {
    bool used;
    bool closable;          /* a janela do terminal nao pode ser fechada (e a shell do sistema) */
    enum wm_kind kind;
    int32_t x, y, w, h;     /* w/h incluem a barra de titulo */
    char title[WM_TITLE_MAX];
};

enum wm_hit {
    WM_HIT_NONE = 0,
    WM_HIT_TITLEBAR,
    WM_HIT_CLOSE,
    WM_HIT_BODY
};

enum wm_start_item {
    WM_START_NONE = 0,
    WM_START_TERMINAL,
    WM_START_CALC,
    WM_START_FILES,
    WM_START_SOBRE
};

struct wm_state {
    struct wm_window windows[WM_MAX_WINDOWS];
    int order[WM_MAX_WINDOWS];      /* ids em ordem Z, do fundo (indice 0) para a frente */
    int count;
    int32_t screen_w, screen_h;

    int dragging_id;                /* -1 se nenhuma janela esta sendo arrastada */
    int32_t drag_off_x, drag_off_y; /* deslocamento do mouse ate o canto da janela */
    bool prev_left;                 /* botao esquerdo no tick anterior (detecta clique/solta) */

    bool start_menu_open;
};

/* O que aconteceu num wm_handle_mouse(): o chamador (desktop.c) usa para agir no que a
 * logica pura aqui nao pode fazer sozinha (fechar de verdade nao precisa, ja e feito; mas
 * abrir uma janela nova, focar o terminal ou injetar um comando no console, sim). */
struct wm_click_result {
    bool dirty;                     /* precisa recompor a tela */
    int closed_id;                  /* id fechado agora pelo botao "x", ou -1 */
    enum wm_start_item start_item;  /* item do menu iniciar escolhido agora, ou WM_START_NONE */
};

void wm_init(struct wm_state *wm, int32_t screen_w, int32_t screen_h);

/* Cria uma janela e a traz para frente. Devolve o id (indice estavel ate ser fechada) ou -1
 * se a tabela de janelas estiver cheia. */
int wm_add_window(struct wm_state *wm, const char *title, enum wm_kind kind, bool closable,
                   int32_t x, int32_t y, int32_t w, int32_t h);
void wm_close_window(struct wm_state *wm, int id);
bool wm_window_exists(const struct wm_state *wm, int id);
const struct wm_window *wm_window(const struct wm_state *wm, int id);

/* Percorre a ordem Z do fundo (z = 0) para a frente (z = wm->count - 1); devolve o id, ou -1
 * se z estiver fora da faixa. Para desenhar na ordem certa. */
int wm_window_id_at_z(const struct wm_state *wm, int z);
int wm_focused_id(const struct wm_state *wm);   /* topo da pilha Z, ou -1 se nao ha janelas */

void wm_raise(struct wm_state *wm, int id);

/* Qual janela (e qual parte dela) esta nas coordenadas dadas, varrendo de frente para tras.
 * Nao considera a area ocupada pela barra de tarefas/menu iniciar. */
int wm_hit_test(const struct wm_state *wm, int32_t mx, int32_t my, enum wm_hit *out_hit);

/* Geometria da barra de tarefas, do botao "Iniciar" e do menu (quando aberto, item 0..3 =
 * Terminal/Calculadora/Arquivos/Sobre, de cima para baixo) -- usada tanto para desenhar
 * quanto, aqui dentro, para o hit-test do clique usar exatamente os mesmos retangulos. */
void wm_taskbar_rect(const struct wm_state *wm, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
void wm_start_button_rect(const struct wm_state *wm, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
void wm_start_item_rect(const struct wm_state *wm, int index,
                         int32_t *x, int32_t *y, int32_t *w, int32_t *h);

/* Processa um "tick" do mouse (posicao atual e se o botao esquerdo esta pressionado agora).
 * Cuida de tudo que e so geometria/estado: iniciar/continuar/soltar o arrasto pela barra de
 * titulo, focar a janela clicada, fechar pelo botao "x", abrir/fechar o menu iniciar e
 * escolher um item dele. O que o item escolhido ou o fechamento devem *fazer* de fato (focar
 * o terminal de verdade, injetar um comando, etc.) fica por conta de quem chamou. */
struct wm_click_result wm_handle_mouse(struct wm_state *wm, int32_t mx, int32_t my, bool left_down);

#endif
