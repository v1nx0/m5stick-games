// Space Dodge -- part of the App Store game pack. Controls work on wheel boards
// (Up/Down/OK/Back) and on D-pad boards (all keys).
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

static bool dpad;
static inline uint32_t now_ms(void) {
    return (uint32_t)((uint64_t)furi_get_tick() * 1000u / furi_kernel_get_tick_frequency());
}
static inline bool is_step(const InputEvent* e) {
    return e->type == InputTypePress || e->type == InputTypeRepeat;
}

#define TICK_MS 30
#define ROCKS 6
#define SHIP_X 6

typedef struct {
    FuriMutex* mutex;
    int y; // ship top, 6px tall
    int rx[ROCKS], ry[ROCKS], rs[ROCKS];
    uint32_t ticks, best;
    bool over;
} G;

static void spawn(G* g, int i) {
    g->rx[i] = 128 + rand() % 64;
    g->ry[i] = rand() % 58;
    g->rs[i] = 4 + rand() % 4;
}

static void g_reset(G* g) {
    g->y = 29;
    g->ticks = 0;
    g->over = false;
    for(int i = 0; i < ROCKS; i++) spawn(g, i);
}

static void g_tick(G* g) {
    if(g->over) return;
    g->ticks++;
    int speed = 1 + (int)(g->ticks / 300); // speeds up every ~9 s
    for(int i = 0; i < ROCKS; i++) {
        g->rx[i] -= speed;
        if(g->rx[i] + g->rs[i] < 0) spawn(g, i);
        bool hx = g->rx[i] < SHIP_X + 8 && g->rx[i] + g->rs[i] > SHIP_X;
        bool hy = g->ry[i] < g->y + 6 && g->ry[i] + g->rs[i] > g->y;
        if(hx && hy) {
            g->over = true;
            if(g->ticks / 10 > g->best) g->best = g->ticks / 10;
        }
    }
}

static void g_input(G* g, InputEvent* e) {
    if(g->over) {
        if(e->key == InputKeyOk && e->type == InputTypeShort) g_reset(g);
        return;
    }
    if(!is_step(e)) return;
    if(e->key == InputKeyUp || e->key == InputKeyLeft) g->y -= 5;
    if(e->key == InputKeyDown || e->key == InputKeyRight) g->y += 5;
    if(g->y < 0) g->y = 0;
    if(g->y > 58) g->y = 58;
}

static void g_draw(Canvas* c, G* g) {
    // ship from lines: canvas_draw_triangle is not exported to apps on this firmware
    canvas_draw_line(c, SHIP_X, g->y, SHIP_X + 8, g->y + 3);
    canvas_draw_line(c, SHIP_X, g->y + 6, SHIP_X + 8, g->y + 3);
    canvas_draw_line(c, SHIP_X, g->y, SHIP_X, g->y + 6);
    for(int i = 0; i < ROCKS; i++) canvas_draw_rframe(c, g->rx[i], g->ry[i], g->rs[i], g->rs[i], 1);
    char b[32];
    snprintf(b, sizeof(b), "%lu", (unsigned long)(g->ticks / 10));
    canvas_draw_str_aligned(c, 126, 1, AlignRight, AlignTop, b);
    if(g->over) {
        snprintf(b, sizeof(b), "Crash! best %lu  OK", (unsigned long)g->best);
        canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignCenter, b);
    }
}


static void draw_cb(Canvas* c, void* ctx) {
    G* g = ctx;
    furi_mutex_acquire(g->mutex, FuriWaitForever);
    canvas_clear(c);
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontSecondary);
    g_draw(c, g);
    furi_mutex_release(g->mutex);
}

static void input_cb(InputEvent* e, void* ctx) {
    furi_message_queue_put(ctx, e, 0);
}

int32_t space_dodge_app(void* p) {
    UNUSED(p);
    dpad = false; // M5StickC Plus2: A / B / Power buttons, no D-pad
    srand(furi_get_tick());
    G* g = malloc(sizeof(G));
    memset(g, 0, sizeof(G));
    g->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    g_reset(g);

    FuriMessageQueue* q = furi_message_queue_alloc(16, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_cb, g);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    uint32_t last = now_ms();
    for(bool run = true; run;) {
        InputEvent e;
        bool dirty = false;
        if(furi_message_queue_get(q, &e, furi_ms_to_ticks(10)) == FuriStatusOk) {
            if(e.key == InputKeyBack && e.type == InputTypeShort) {
                run = false;
            } else {
                furi_mutex_acquire(g->mutex, FuriWaitForever);
                g_input(g, &e);
                furi_mutex_release(g->mutex);
                dirty = true;
            }
        }
        if(now_ms() - last >= TICK_MS) {
            last = now_ms();
            furi_mutex_acquire(g->mutex, FuriWaitForever);
            g_tick(g);
            furi_mutex_release(g->mutex);
            dirty = true;
        }
        if(dirty) view_port_update(vp);
    }

    view_port_enabled_set(vp, false);
    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_message_queue_free(q);
    furi_mutex_free(g->mutex);
    free(g);
    return 0;
}
