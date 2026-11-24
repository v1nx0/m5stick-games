// Reaction Test -- part of the App Store game pack. Controls work on wheel boards
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

#define TICK_MS 5
typedef enum { RIdle, RWait, RGo, RResult, RTooSoon } RState;

typedef struct {
    FuriMutex* mutex;
    RState st;
    uint32_t go_at, result, best;
} G;

static void g_reset(G* g) {
    g->st = RIdle;
}

static void g_tick(G* g) {
    if(g->st == RWait && now_ms() >= g->go_at) {
        g->st = RGo;
        g->go_at = now_ms();
    }
}

static void g_input(G* g, InputEvent* e) {
    if(e->key != InputKeyOk || e->type != InputTypePress) return;
    switch(g->st) {
    case RWait:
        g->st = RTooSoon;
        break;
    case RGo:
        g->result = now_ms() - g->go_at;
        if(!g->best || g->result < g->best) g->best = g->result;
        g->st = RResult;
        break;
    default:
        g->st = RWait;
        g->go_at = now_ms() + 1500 + rand() % 2500;
    }
}

static void g_draw(Canvas* c, G* g) {
    char b[32];
    canvas_set_font(c, FontPrimary);
    switch(g->st) {
    case RIdle:
        canvas_draw_str_aligned(c, 64, 26, AlignCenter, AlignCenter, "Reaction Test");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 42, AlignCenter, AlignCenter, "OK, then OK on GO!");
        break;
    case RWait:
        canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignCenter, "Wait...");
        break;
    case RGo:
        canvas_draw_box(c, 0, 0, 128, 64);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignCenter, "GO!");
        break;
    case RTooSoon:
        canvas_draw_str_aligned(c, 64, 26, AlignCenter, AlignCenter, "Too soon!");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 42, AlignCenter, AlignCenter, "OK to retry");
        break;
    case RResult:
        snprintf(b, sizeof(b), "%lu ms", (unsigned long)g->result);
        canvas_draw_str_aligned(c, 64, 24, AlignCenter, AlignCenter, b);
        canvas_set_font(c, FontSecondary);
        snprintf(b, sizeof(b), "best %lu ms - OK again", (unsigned long)g->best);
        canvas_draw_str_aligned(c, 64, 42, AlignCenter, AlignCenter, b);
        break;
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

int32_t reaction_test_app(void* p) {
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
