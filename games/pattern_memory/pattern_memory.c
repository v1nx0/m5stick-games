// Pattern Memory -- part of the App Store game pack. Controls work on wheel boards
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

#define TICK_MS 50
#define SEQ_MAX 64
typedef enum { PIdle, PShow, PInput, PFail } PState;

typedef struct {
    FuriMutex* mutex;
    uint8_t seq[SEQ_MAX];
    int len, pos, cur, best, lit;
    uint32_t t;
    PState st;
} G;

static void next_round(G* g) {
    if(g->len < SEQ_MAX) g->seq[g->len++] = rand() % 4;
    g->pos = 0;
    g->t = 0;
    g->lit = -1;
    g->st = PShow;
}

static void g_reset(G* g) {
    g->len = 0;
    g->st = PIdle;
    g->lit = -1;
    g->cur = 0;
}

static void g_tick(G* g) {
    if(g->st == PShow) {
        // each step: 8 ticks lit, 4 ticks dark (50 ms ticks)
        uint32_t step = g->t / 12, phase = g->t % 12;
        if((int)step >= g->len) {
            g->st = PInput;
            g->pos = 0;
            g->lit = -1;
        } else {
            g->lit = phase < 8 ? g->seq[step] : -1;
        }
        g->t++;
    } else if(g->st == PInput && g->lit >= 0 && ++g->t > 4) {
        g->lit = -1; // short flash for the player's own press
    }
}

static void g_input(G* g, InputEvent* e) {
    if(g->st == PIdle || g->st == PFail) {
        if(e->key == InputKeyOk && e->type == InputTypeShort) {
            g_reset(g);
            next_round(g);
        }
        return;
    }
    if(g->st != PInput) return;
    if(e->key == InputKeyOk && e->type == InputTypePress) {
        g->lit = g->cur;
        g->t = 0;
        if(g->seq[g->pos] != g->cur) {
            g->st = PFail;
            if(g->len - 1 > g->best) g->best = g->len - 1;
        } else if(++g->pos == g->len) {
            if(g->len > g->best) g->best = g->len;
            next_round(g);
        }
    } else if(is_step(e)) {
        if(dpad) {
            if(e->key == InputKeyLeft || e->key == InputKeyRight) g->cur ^= 1;
            if(e->key == InputKeyUp || e->key == InputKeyDown) g->cur ^= 2;
        } else {
            g->cur = (g->cur + (e->key == InputKeyUp ? 3 : 1)) % 4;
        }
    }
}

static void g_draw(Canvas* c, G* g) {
    for(int i = 0; i < 4; i++) {
        int x = 4 + (i % 2) * 30, y = 2 + (i / 2) * 30;
        if(g->lit == i)
            canvas_draw_rbox(c, x, y, 28, 28, 4);
        else
            canvas_draw_rframe(c, x, y, 28, 28, 4);
        if(g->st == PInput && g->cur == i) canvas_draw_rframe(c, x + 3, y + 3, 22, 22, 3);
    }
    char b[24];
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 68, 12, "Pattern");
    canvas_set_font(c, FontSecondary);
    snprintf(b, sizeof(b), "Round %d", g->len);
    canvas_draw_str(c, 68, 26, b);
    snprintf(b, sizeof(b), "Best %d", g->best);
    canvas_draw_str(c, 68, 36, b);
    const char* m = g->st == PIdle ? "OK to start" :
                    g->st == PShow ? "Watch..." :
                    g->st == PInput ? "Repeat it!" :
                                      "Wrong! OK";
    canvas_draw_str(c, 68, 52, m);
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

int32_t pattern_memory_app(void* p) {
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
        // sleep until the next game tick is due (or a key arrives): no 100 Hz polling
        uint32_t since = now_ms() - last;
        uint32_t wait = since >= TICK_MS ? 0 : TICK_MS - since;
        if(furi_message_queue_get(q, &e, furi_ms_to_ticks(wait)) == FuriStatusOk) {
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
