// Star Shooter -- part of the App Store game pack. Controls work on wheel boards
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
#define FOES 5
#define SHOTS 4
#define SHIP_X 4

typedef struct {
    FuriMutex* mutex;
    int y;
    int fx[FOES], fy[FOES];
    int sx[SHOTS], sy[SHOTS];
    bool shot[SHOTS];
    int score, lives;
    uint32_t ticks;
    bool over;
} G;

static void spawn(G* g, int i) {
    g->fx[i] = 128 + rand() % 96;
    g->fy[i] = 2 + rand() % 54;
}

static void g_reset(G* g) {
    g->y = 28;
    g->score = 0;
    g->lives = 3;
    g->ticks = 0;
    g->over = false;
    for(int i = 0; i < FOES; i++) spawn(g, i);
    for(int i = 0; i < SHOTS; i++) g->shot[i] = false;
}

static void g_tick(G* g) {
    if(g->over) return;
    g->ticks++;
    for(int s = 0; s < SHOTS; s++)
        if(g->shot[s] && (g->sx[s] += 4) > 128) g->shot[s] = false;
    bool move = g->ticks % (g->score > 30 ? 1 : 2) == 0; // faster after 30 kills
    for(int i = 0; i < FOES; i++) {
        if(move) g->fx[i]--;
        for(int s = 0; s < SHOTS; s++) {
            if(g->shot[s] && g->sx[s] + 3 >= g->fx[i] && g->sx[s] <= g->fx[i] + 7 &&
               g->sy[s] >= g->fy[i] && g->sy[s] <= g->fy[i] + 6) {
                g->shot[s] = false;
                g->score++;
                spawn(g, i);
            }
        }
        bool hit_ship = g->fx[i] < SHIP_X + 8 && g->fy[i] < g->y + 6 && g->fy[i] + 6 > g->y;
        if(g->fx[i] < 0 || hit_ship) {
            spawn(g, i);
            if(--g->lives <= 0) g->over = true;
        }
    }
}

static void g_input(G* g, InputEvent* e) {
    if(g->over) {
        if(e->key == InputKeyOk && e->type == InputTypeShort) g_reset(g);
        return;
    }
    if(e->key == InputKeyOk && e->type == InputTypePress) {
        for(int s = 0; s < SHOTS; s++)
            if(!g->shot[s]) {
                g->shot[s] = true;
                g->sx[s] = SHIP_X + 8;
                g->sy[s] = g->y + 3;
                break;
            }
        return;
    }
    if(!is_step(e)) return;
    if(e->key == InputKeyUp || e->key == InputKeyLeft) g->y -= 5;
    if(e->key == InputKeyDown || e->key == InputKeyRight) g->y += 5;
    if(g->y < 0) g->y = 0;
    if(g->y > 57) g->y = 57;
}

static void g_draw(Canvas* c, G* g) {
    canvas_draw_triangle(c, SHIP_X + 4, g->y + 3, 8, 6, CanvasDirectionLeftToRight);
    for(int s = 0; s < SHOTS; s++)
        if(g->shot[s]) canvas_draw_line(c, g->sx[s], g->sy[s], g->sx[s] + 3, g->sy[s]);
    for(int i = 0; i < FOES; i++) {
        canvas_draw_frame(c, g->fx[i], g->fy[i], 7, 6);
        canvas_draw_dot(c, g->fx[i] + 3, g->fy[i] + 2);
    }
    char b[32];
    snprintf(b, sizeof(b), "%d  lives %d", g->score, g->lives);
    canvas_draw_str_aligned(c, 126, 1, AlignRight, AlignTop, b);
    if(g->over) canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignCenter, "Game Over - OK");
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

int32_t star_shooter_app(void* p) {
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
