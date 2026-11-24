// Hi-Lo -- part of the App Store game pack. Controls work on wheel boards
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

#define TICK_MS 1000
typedef struct {
    FuriMutex* mutex;
    int card, prev, streak, best;
    int last; // 0 none, 1 right, 2 wrong, 3 tie
} G;

static const char* RANKS[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

static void g_reset(G* g) {
    g->card = 1 + rand() % 13;
    g->prev = 0;
    g->streak = 0;
    g->last = 0;
}

static void g_tick(G* g) {
    UNUSED(g);
}

static void g_input(G* g, InputEvent* e) {
    if(e->type != InputTypeShort) return;
    int guess = 0; // +1 higher, -1 lower
    if(e->key == InputKeyUp || e->key == InputKeyRight) guess = 1;
    if(e->key == InputKeyDown || e->key == InputKeyLeft) guess = -1;
    if(e->key == InputKeyOk && g->last == 2) {
        g_reset(g);
        return;
    }
    if(!guess || g->last == 2) return;
    int next = 1 + rand() % 13;
    g->prev = g->card;
    g->card = next;
    if(next == g->prev) {
        g->last = 3; // tie: streak kept
    } else if((next > g->prev) == (guess > 0)) {
        g->last = 1;
        if(++g->streak > g->best) g->best = g->streak;
    } else {
        g->last = 2;
    }
}

static void g_draw(Canvas* c, G* g) {
    canvas_draw_rframe(c, 8, 8, 34, 48, 4);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 25, 32, AlignCenter, AlignCenter, RANKS[g->card]);
    canvas_draw_str(c, 52, 12, "Hi-Lo");
    canvas_set_font(c, FontSecondary);
    char b[32];
    snprintf(b, sizeof(b), "Streak %d  best %d", g->streak, g->best);
    canvas_draw_str(c, 52, 24, b);
    if(g->prev) {
        snprintf(b, sizeof(b), "Last card: %s", RANKS[g->prev]);
        canvas_draw_str(c, 52, 34, b);
    }
    const char* m = g->last == 1 ? "Right!" : g->last == 2 ? "Wrong! OK = new" : g->last == 3 ? "Tie - safe" : "";
    canvas_draw_str(c, 52, 46, m);
    canvas_draw_str(c, 46, 58, "Up=higher Down=lower");
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

int32_t hi_lo_app(void* p) {
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
