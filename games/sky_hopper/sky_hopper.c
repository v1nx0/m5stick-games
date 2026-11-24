// Sky Hopper: tap OK/Up to hop, fly through the gaps between columns.
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

#define W 128
#define H 64
#define COLS 3
#define COL_W 8
#define COL_GAP 22
#define COL_SPACING 48
#define HOP_X 24
#define R 3
#define FP 16 // fixed-point scale for y / velocity

typedef struct {
    int16_t x, gap_y;
    bool scored;
} Col;

typedef struct {
    int32_t y, vy; // FP units
    Col cols[COLS];
    uint16_t score, best;
    bool started, dead;
    FuriMutex* mutex;
} Game;

static void reset(Game* g) {
    g->y = (H / 2) * FP;
    g->vy = 0;
    g->score = 0;
    g->started = g->dead = false;
    for(int i = 0; i < COLS; i++) {
        g->cols[i].x = W + i * COL_SPACING;
        g->cols[i].gap_y = 8 + rand() % (H - COL_GAP - 16);
        g->cols[i].scored = false;
    }
}

static void tick(Game* g) {
    if(!g->started || g->dead) return;
    g->vy += 4; // gravity
    if(g->vy > 40) g->vy = 40;
    g->y += g->vy;
    int y = g->y / FP;
    if(y - R < 0 || y + R >= H) g->dead = true;
    for(int i = 0; i < COLS; i++) {
        Col* c = &g->cols[i];
        c->x -= 1;
        if(c->x + COL_W < 0) {
            c->x += COLS * COL_SPACING;
            c->gap_y = 8 + rand() % (H - COL_GAP - 16);
            c->scored = false;
        }
        bool overlap_x = HOP_X + R > c->x && HOP_X - R < c->x + COL_W;
        bool in_gap = y - R >= c->gap_y && y + R <= c->gap_y + COL_GAP;
        if(overlap_x && !in_gap) g->dead = true;
        if(!c->scored && c->x + COL_W < HOP_X - R) {
            c->scored = true;
            g->score++;
        }
    }
    if(g->dead && g->score > g->best) g->best = g->score;
}

static void hop(Game* g) {
    if(g->dead) {
        reset(g);
        return;
    }
    g->started = true;
    g->vy = -26;
}

static void draw_cb(Canvas* canvas, void* ctx) {
    Game* g = ctx;
    furi_mutex_acquire(g->mutex, FuriWaitForever);
    canvas_clear(canvas);
    for(int i = 0; i < COLS; i++) {
        Col* c = &g->cols[i];
        canvas_draw_box(canvas, c->x, 0, COL_W, c->gap_y);
        canvas_draw_box(canvas, c->x, c->gap_y + COL_GAP, COL_W, H - c->gap_y - COL_GAP);
    }
    canvas_draw_disc(canvas, HOP_X, g->y / FP, R);
    char buf[24];
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%u", g->score);
    canvas_draw_str_aligned(canvas, W - 2, 2, AlignRight, AlignTop, buf);
    if(!g->started || g->dead) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 24, 18, 80, 28);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 24, 18, 80, 28);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, 64, 28, AlignCenter, AlignCenter, g->dead ? "Game Over" : "Sky Hopper");
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "OK to hop  best %u", g->best);
        canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignCenter, buf);
    }
    furi_mutex_release(g->mutex);
}

static void input_cb(InputEvent* e, void* ctx) {
    furi_message_queue_put(ctx, e, FuriWaitForever);
}

int32_t sky_hopper_app(void* p) {
    UNUSED(p);
    Game* g = malloc(sizeof(Game));
    g->best = 0;
    srand(furi_get_tick()); // different column layout every launch
    g->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    reset(g);

    FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_cb, g);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    InputEvent e;
    for(bool run = true; run;) {
        // ~33 fps; physics advances once per frame
        if(furi_message_queue_get(q, &e, 30) == FuriStatusOk) {
            if(e.type == InputTypePress) {
                furi_mutex_acquire(g->mutex, FuriWaitForever);
                if(e.key == InputKeyBack)
                    run = false;
                else if(e.key == InputKeyOk || e.key == InputKeyUp || e.key == InputKeyRight)
                    hop(g);
                furi_mutex_release(g->mutex);
            }
        } else {
            furi_mutex_acquire(g->mutex, FuriWaitForever);
            tick(g);
            furi_mutex_release(g->mutex);
        }
        view_port_update(vp);
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
