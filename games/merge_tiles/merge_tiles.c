// Merge Tiles -- part of the App Store game pack. Controls work on wheel boards
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
    uint8_t t[16]; // exponent: 0 empty, 1 = 2, 2 = 4 ...
    uint32_t score, best;
    bool over;
} G;

static void add_tile(G* g) {
    int empty[16], n = 0;
    for(int i = 0; i < 16; i++)
        if(!g->t[i]) empty[n++] = i;
    if(n) g->t[empty[rand() % n]] = rand() % 10 ? 1 : 2;
}

static void g_reset(G* g) {
    memset(g->t, 0, sizeof(g->t));
    g->score = 0;
    g->over = false;
    add_tile(g);
    add_tile(g);
}

// Slides 4 cells (given as board indices, toward idx[0]); returns true if anything moved.
static bool slide(G* g, const int idx[4]) {
    uint8_t v[4], out[4] = {0};
    int n = 0;
    for(int i = 0; i < 4; i++)
        if(g->t[idx[i]]) v[n++] = g->t[idx[i]];
    int o = 0;
    for(int i = 0; i < n; i++) {
        if(i + 1 < n && v[i] == v[i + 1]) {
            out[o++] = v[i] + 1;
            g->score += 1u << (v[i] + 1);
            i++;
        } else {
            out[o++] = v[i];
        }
    }
    bool moved = false;
    for(int i = 0; i < 4; i++) {
        if(g->t[idx[i]] != out[i]) moved = true;
        g->t[idx[i]] = out[i];
    }
    return moved;
}

// dir: 0 up, 1 down, 2 left, 3 right
static bool move(G* g, int dir) {
    bool moved = false;
    for(int k = 0; k < 4; k++) {
        int idx[4];
        for(int i = 0; i < 4; i++) {
            if(dir == 0) idx[i] = i * 4 + k;
            if(dir == 1) idx[i] = (3 - i) * 4 + k;
            if(dir == 2) idx[i] = k * 4 + i;
            if(dir == 3) idx[i] = k * 4 + 3 - i;
        }
        moved |= slide(g, idx);
    }
    return moved;
}

static bool can_move(G* g) {
    for(int i = 0; i < 16; i++) {
        if(!g->t[i]) return true;
        if(i % 4 < 3 && g->t[i] == g->t[i + 1]) return true;
        if(i < 12 && g->t[i] == g->t[i + 4]) return true;
    }
    return false;
}

static void g_tick(G* g) {
    UNUSED(g);
}

static void g_input(G* g, InputEvent* e) {
    if(g->over) {
        if(e->key == InputKeyOk && e->type == InputTypeShort) g_reset(g);
        return;
    }
    int dir = -1;
    if(e->type == InputTypeShort) {
        if(e->key == InputKeyUp) dir = 0;
        if(e->key == InputKeyDown) dir = 1;
        if(e->key == InputKeyLeft) dir = 2;
        if(e->key == InputKeyRight) dir = 3;
        if(e->key == InputKeyOk && !dpad) dir = 2; // wheel boards: OK = left
    } else if(e->type == InputTypeLong && e->key == InputKeyOk && !dpad) {
        dir = 3; // wheel boards: hold OK = right
    }
    if(dir < 0) return;
    if(move(g, dir)) add_tile(g);
    if(g->score > g->best) g->best = g->score;
    if(!can_move(g)) g->over = true;
}

static void g_draw(Canvas* c, G* g) {
    char b[24];
    for(int i = 0; i < 16; i++) {
        int x = (i % 4) * 20, y = (i / 4) * 16;
        canvas_draw_frame(c, x, y, 21, 17);
        if(!g->t[i]) continue;
        if(g->t[i] >= 7) { // 128+ drawn inverted so big tiles stand out
            canvas_draw_box(c, x + 1, y + 1, 19, 15);
            canvas_set_color(c, ColorWhite);
        }
        snprintf(b, sizeof(b), "%lu", (unsigned long)(1ul << g->t[i]));
        canvas_draw_str_aligned(c, x + 11, y + 9, AlignCenter, AlignCenter, b);
        canvas_set_color(c, ColorBlack);
    }
    snprintf(b, sizeof(b), "%lu", (unsigned long)g->score);
    canvas_draw_str(c, 86, 10, "Score");
    canvas_draw_str(c, 86, 20, b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)g->best);
    canvas_draw_str(c, 86, 32, "Best");
    canvas_draw_str(c, 86, 42, b);
    if(g->over)
        canvas_draw_str(c, 86, 58, "Over! OK");
    else if(!dpad)
        canvas_draw_str(c, 84, 58, "OK L/hold R");
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

int32_t merge_tiles_app(void* p) {
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
