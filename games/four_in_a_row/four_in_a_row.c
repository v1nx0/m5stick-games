// Four in a Row -- part of the App Store game pack. Controls work on wheel boards
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
#define FC 7
#define FR 6
typedef struct {
    FuriMutex* mutex;
    uint8_t b[FR][FC]; // 0 empty, 1 you, 2 cpu
    int col, wins, losses;
    uint8_t winner; // 0 playing, 1, 2, 3 = draw
} G;

static void g_reset(G* g) {
    memset(g->b, 0, sizeof(g->b));
    g->col = FC / 2;
    g->winner = 0;
}

static int drop_row(G* g, int c) {
    for(int r = FR - 1; r >= 0; r--)
        if(!g->b[r][c]) return r;
    return -1;
}

static bool wins_at(G* g, int r, int c) {
    static const int D[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    uint8_t p = g->b[r][c];
    for(int d = 0; d < 4; d++) {
        int n = 1;
        for(int s = -1; s <= 1; s += 2)
            for(int k = 1; k < 4; k++) {
                int rr = r + D[d][0] * k * s, cc = c + D[d][1] * k * s;
                if(rr < 0 || rr >= FR || cc < 0 || cc >= FC || g->b[rr][cc] != p) break;
                n++;
            }
        if(n >= 4) return true;
    }
    return false;
}

// Drop p into column c; returns true if that wins. Sets winner on win/draw.
static bool play(G* g, int c, uint8_t p) {
    int r = drop_row(g, c);
    g->b[r][c] = p;
    if(wins_at(g, r, c)) {
        g->winner = p;
        return true;
    }
    bool full = true;
    for(int k = 0; k < FC; k++)
        if(drop_row(g, k) >= 0) full = false;
    if(full) g->winner = 3;
    return false;
}

static int try_win(G* g, uint8_t p) {
    for(int c = 0; c < FC; c++) {
        int r = drop_row(g, c);
        if(r < 0) continue;
        g->b[r][c] = p;
        bool w = wins_at(g, r, c);
        g->b[r][c] = 0;
        if(w) return c;
    }
    return -1;
}

static void cpu_move(G* g) {
    int c = try_win(g, 2);
    if(c < 0) c = try_win(g, 1);
    if(c < 0) {
        static const int pref[FC] = {3, 2, 4, 1, 5, 0, 6};
        for(int tries = 0; tries < 20 && c < 0; tries++) {
            int k = pref[rand() % (tries < 10 ? 3 : FC)];
            if(drop_row(g, k) >= 0) c = k;
        }
        for(int k = 0; k < FC && c < 0; k++)
            if(drop_row(g, k) >= 0) c = k;
    }
    if(c >= 0) play(g, c, 2);
}

static void g_tick(G* g) {
    UNUSED(g);
}

static void g_input(G* g, InputEvent* e) {
    if(e->key == InputKeyOk && e->type == InputTypeShort) {
        if(g->winner) {
            if(g->winner == 1) g->wins++;
            if(g->winner == 2) g->losses++;
            g_reset(g);
            return;
        }
        if(drop_row(g, g->col) < 0) return;
        play(g, g->col, 1);
        if(!g->winner) cpu_move(g);
    } else if(is_step(e)) {
        if(e->key == InputKeyLeft || e->key == InputKeyUp) g->col = (g->col + FC - 1) % FC;
        if(e->key == InputKeyRight || e->key == InputKeyDown) g->col = (g->col + 1) % FC;
    }
}

static void g_draw(Canvas* c, G* g) {
    const int ox = 2, oy = 14, s = 8;
    canvas_draw_frame(c, ox - 1, oy - 1, FC * s + 2, FR * s + 2);
    for(int r = 0; r < FR; r++)
        for(int k = 0; k < FC; k++) {
            int x = ox + k * s + 3, y = oy + r * s + 3;
            if(g->b[r][k] == 1) canvas_draw_disc(c, x, y, 3);
            else if(g->b[r][k] == 2) canvas_draw_circle(c, x, y, 3);
        }
    if(!g->winner)
    {
        int x = ox + g->col * s + 3; // down arrow from lines: canvas_draw_triangle is not exported to apps
        canvas_draw_line(c, x - 3, oy - 5, x + 3, oy - 5);
        canvas_draw_line(c, x - 3, oy - 5, x, oy - 2);
        canvas_draw_line(c, x + 3, oy - 5, x, oy - 2);
    }
    char b[32];
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 62, 10, "Four in a Row");
    canvas_set_font(c, FontSecondary);
    snprintf(b, sizeof(b), "You %d  CPU %d", g->wins, g->losses);
    canvas_draw_str(c, 62, 24, b);
    canvas_draw_str(c, 62, 36, "You = filled");
    if(g->winner)
        canvas_draw_str(
            c, 62, 50, g->winner == 1 ? "You win! OK" : g->winner == 2 ? "CPU wins. OK" : "Draw. OK");
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

int32_t four_in_a_row_app(void* p) {
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
