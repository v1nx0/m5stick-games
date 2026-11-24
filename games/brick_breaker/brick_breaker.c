// Brick Breaker -- part of the App Store game pack. Controls work on wheel boards
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

#define TICK_MS 20
#define BR_ROWS 4
#define BR_COLS 10
#define PAD_W 20
#define PAD_Y 60

typedef struct {
    FuriMutex* mutex;
    float bx, by, vx, vy;
    int px;
    bool bricks[BR_ROWS][BR_COLS];
    int left, score, lives;
    bool launched, over;
} G;

static void g_serve(G* g) {
    g->launched = false;
    g->vx = 1.0f;
    g->vy = -1.5f;
}

static void g_reset(G* g) {
    for(int r = 0; r < BR_ROWS; r++)
        for(int c = 0; c < BR_COLS; c++) g->bricks[r][c] = true;
    g->left = BR_ROWS * BR_COLS;
    g->score = 0;
    g->lives = 3;
    g->over = false;
    g->px = 64 - PAD_W / 2;
    g_serve(g);
}

static void g_tick(G* g) {
    if(g->over) return;
    if(!g->launched) {
        g->bx = g->px + PAD_W / 2;
        g->by = PAD_Y - 3;
        return;
    }
    g->bx += g->vx;
    g->by += g->vy;
    if(g->bx < 1) g->bx = 1, g->vx = -g->vx;
    if(g->bx > 126) g->bx = 126, g->vx = -g->vx;
    if(g->by < 1) g->by = 1, g->vy = -g->vy;
    if(g->vy > 0 && g->by >= PAD_Y - 2 && g->by <= PAD_Y + 1 && g->bx >= g->px - 1 &&
       g->bx <= g->px + PAD_W + 1) {
        g->vy = -g->vy;
        g->vx = (g->bx - (g->px + PAD_W / 2.0f)) / (PAD_W / 2.0f) * 1.6f;
        if(g->vx > -0.3f && g->vx < 0.3f) g->vx = g->vx < 0 ? -0.3f : 0.3f;
    }
    if(g->by > 64) {
        if(--g->lives <= 0) g->over = true;
        g_serve(g);
        return;
    }
    int c = ((int)g->bx - 4) / 12, r = ((int)g->by - 8) / 5;
    if(g->bx >= 4 && g->by >= 8 && c < BR_COLS && r < BR_ROWS && g->bricks[r][c]) {
        g->bricks[r][c] = false;
        g->vy = -g->vy;
        g->score++;
        if(--g->left == 0) g->over = true;
    }
}

static void g_input(G* g, InputEvent* e) {
    if(e->key == InputKeyOk && e->type == InputTypeShort) {
        if(g->over)
            g_reset(g);
        else
            g->launched = true;
    } else if(is_step(e)) {
        int d = (e->key == InputKeyLeft || e->key == InputKeyUp)     ? -6 :
                (e->key == InputKeyRight || e->key == InputKeyDown) ? 6 :
                                                                      0;
        g->px += d;
        if(g->px < 0) g->px = 0;
        if(g->px > 128 - PAD_W) g->px = 128 - PAD_W;
    }
}

static void g_draw(Canvas* c, G* g) {
    for(int r = 0; r < BR_ROWS; r++)
        for(int k = 0; k < BR_COLS; k++)
            if(g->bricks[r][k]) canvas_draw_box(c, 4 + k * 12, 8 + r * 5, 11, 4);
    canvas_draw_box(c, g->px, PAD_Y, PAD_W, 2);
    canvas_draw_disc(c, (int)g->bx, (int)g->by, 1);
    char b[32];
    snprintf(b, sizeof(b), "%d  lives %d", g->score, g->lives);
    canvas_draw_str(c, 2, 6, b);
    if(g->over)
        canvas_draw_str_aligned(
            c, 64, 40, AlignCenter, AlignCenter, g->left ? "Game Over - OK" : "You win! - OK");
    else if(!g->launched)
        canvas_draw_str_aligned(c, 64, 40, AlignCenter, AlignCenter, "OK to launch");
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

int32_t brick_breaker_app(void* p) {
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
