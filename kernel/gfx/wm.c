/* kernel/gfx/wm.c - ver wm.h. Logica pura: nenhuma chamada de E/S ou de hardware. */
#include "wm.h"
#include "../util.h"

static void title_copy(char *dst, const char *src, size_t max)
{
    size_t i = 0;

    while (src[i] != '\0' && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool point_in(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void order_remove(struct wm_state *wm, int id)
{
    int i;
    int dst = 0;

    for (i = 0; i < wm->count; i++) {
        if (wm->order[i] != id)
            wm->order[dst++] = wm->order[i];
    }
    wm->count = dst;
}

void wm_init(struct wm_state *wm, int32_t screen_w, int32_t screen_h)
{
    int i;

    for (i = 0; i < WM_MAX_WINDOWS; i++)
        wm->windows[i].used = false;
    wm->count = 0;
    wm->screen_w = screen_w;
    wm->screen_h = screen_h;
    wm->dragging_id = -1;
    wm->drag_off_x = 0;
    wm->drag_off_y = 0;
    wm->prev_left = false;
    wm->start_menu_open = false;
}

int wm_add_window(struct wm_state *wm, const char *title, enum wm_kind kind, bool closable,
                   int32_t x, int32_t y, int32_t w, int32_t h)
{
    int id;

    for (id = 0; id < WM_MAX_WINDOWS; id++) {
        if (!wm->windows[id].used)
            break;
    }
    if (id == WM_MAX_WINDOWS)
        return -1;

    wm->windows[id].used = true;
    wm->windows[id].closable = closable;
    wm->windows[id].kind = kind;
    wm->windows[id].x = x;
    wm->windows[id].y = y;
    wm->windows[id].w = w;
    wm->windows[id].h = h;
    title_copy(wm->windows[id].title, title, WM_TITLE_MAX);

    wm->order[wm->count++] = id;
    return id;
}

void wm_close_window(struct wm_state *wm, int id)
{
    if (!wm_window_exists(wm, id))
        return;
    wm->windows[id].used = false;
    order_remove(wm, id);
    if (wm->dragging_id == id)
        wm->dragging_id = -1;
}

bool wm_window_exists(const struct wm_state *wm, int id)
{
    return id >= 0 && id < WM_MAX_WINDOWS && wm->windows[id].used;
}

const struct wm_window *wm_window(const struct wm_state *wm, int id)
{
    if (!wm_window_exists(wm, id))
        return NULL;
    return &wm->windows[id];
}

int wm_window_id_at_z(const struct wm_state *wm, int z)
{
    if (z < 0 || z >= wm->count)
        return -1;
    return wm->order[z];
}

int wm_focused_id(const struct wm_state *wm)
{
    return wm->count > 0 ? wm->order[wm->count - 1] : -1;
}

void wm_raise(struct wm_state *wm, int id)
{
    if (!wm_window_exists(wm, id))
        return;
    order_remove(wm, id);
    wm->order[wm->count++] = id;
}

int wm_hit_test(const struct wm_state *wm, int32_t mx, int32_t my, enum wm_hit *out_hit)
{
    int z;

    for (z = wm->count - 1; z >= 0; z--) {
        int id = wm->order[z];
        const struct wm_window *win = &wm->windows[id];

        if (!point_in(mx, my, win->x, win->y, win->w, win->h))
            continue;

        if (point_in(mx, my, win->x, win->y, win->w, WM_TITLEBAR_H)) {
            if (win->closable) {
                int32_t cx = win->x + win->w - WM_CLOSE_MARGIN - WM_CLOSE_SIZE;
                int32_t cy = win->y + (WM_TITLEBAR_H - WM_CLOSE_SIZE) / 2;

                if (point_in(mx, my, cx, cy, WM_CLOSE_SIZE, WM_CLOSE_SIZE)) {
                    *out_hit = WM_HIT_CLOSE;
                    return id;
                }
            }
            *out_hit = WM_HIT_TITLEBAR;
            return id;
        }
        *out_hit = WM_HIT_BODY;
        return id;
    }
    *out_hit = WM_HIT_NONE;
    return -1;
}

void wm_taskbar_rect(const struct wm_state *wm, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = 0;
    *y = wm->screen_h - WM_TASKBAR_H;
    *w = wm->screen_w;
    *h = WM_TASKBAR_H;
}

void wm_start_button_rect(const struct wm_state *wm, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = 0;
    *y = wm->screen_h - WM_TASKBAR_H;
    *w = WM_START_BTN_W;
    *h = WM_TASKBAR_H;
}

void wm_start_item_rect(const struct wm_state *wm, int index,
                         int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    int32_t panel_h = WM_START_ITEM_COUNT * WM_START_ITEM_H;

    *x = 0;
    *y = wm->screen_h - WM_TASKBAR_H - panel_h + index * WM_START_ITEM_H;
    *w = WM_START_ITEM_W;
    *h = WM_START_ITEM_H;
}

struct wm_click_result wm_handle_mouse(struct wm_state *wm, int32_t mx, int32_t my, bool left_down)
{
    struct wm_click_result res = { false, -1, WM_START_NONE };
    bool just_pressed = left_down && !wm->prev_left;

    if (wm->dragging_id != -1) {
        if (!left_down) {
            wm->dragging_id = -1;
        } else {
            struct wm_window *win = &wm->windows[wm->dragging_id];
            int32_t nx = mx - wm->drag_off_x;
            int32_t ny = my - wm->drag_off_y;

            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx + win->w > wm->screen_w) nx = wm->screen_w - win->w;
            if (ny + win->h > wm->screen_h - WM_TASKBAR_H) ny = wm->screen_h - WM_TASKBAR_H - win->h;
            if (win->x != nx || win->y != ny) {
                win->x = nx;
                win->y = ny;
                res.dirty = true;
            }
        }
        wm->prev_left = left_down;
        return res;
    }

    if (just_pressed) {
        int32_t bx, by, bw, bh;

        wm_start_button_rect(wm, &bx, &by, &bw, &bh);
        if (point_in(mx, my, bx, by, bw, bh)) {
            wm->start_menu_open = !wm->start_menu_open;
            res.dirty = true;
        } else if (wm->start_menu_open) {
            static const enum wm_start_item items[WM_START_ITEM_COUNT] = {
                WM_START_TERMINAL, WM_START_CALC, WM_START_FILES, WM_START_SOBRE
            };
            int i;

            for (i = 0; i < WM_START_ITEM_COUNT; i++) {
                int32_t ix, iy, iw, ih;

                wm_start_item_rect(wm, i, &ix, &iy, &iw, &ih);
                if (point_in(mx, my, ix, iy, iw, ih)) {
                    res.start_item = items[i];
                    break;
                }
            }
            wm->start_menu_open = false;          /* qualquer clique (item ou fora) fecha o menu */
            res.dirty = true;
        } else {
            enum wm_hit hit;
            int id = wm_hit_test(wm, mx, my, &hit);

            if (id != -1) {
                if (hit == WM_HIT_CLOSE) {
                    wm_close_window(wm, id);
                    res.closed_id = id;
                    res.dirty = true;
                } else {
                    wm_raise(wm, id);
                    res.dirty = true;
                    if (hit == WM_HIT_TITLEBAR) {
                        const struct wm_window *win = wm_window(wm, id);

                        wm->dragging_id = id;
                        wm->drag_off_x = mx - win->x;
                        wm->drag_off_y = my - win->y;
                    }
                }
            }
        }
    }

    wm->prev_left = left_down;
    return res;
}
