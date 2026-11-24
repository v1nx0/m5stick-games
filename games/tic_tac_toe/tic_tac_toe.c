// Tic-Tac-Toe -- part of the App Store game pack. Controls work on wheel boards
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
    char b[9];
    int cur, wins, losses, draws;
    char winner; // 0 playing, 'X', 'O', 'D'
} G;

static const int LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

static char check(const char* b) {
    for(int i = 0; i < 8; i++)
        if(b[LINES[i][0]] != ' ' && b[LINES[i][0]] == b[LINES[i][1]] &&
           b[LINES[i][1]] == b[LINES[i][2]])
            return b[LINES[i][0]];
    for(int i = 0; i < 9; i++)
        if(b[i] == ' ') return 0;
    return 'D';
}

static void g_reset(G* g) {
    memset(g->b, ' ', 9);
    g->cur = 4;
    g->winner = 0;
}

static int find_completing(G* g, char who) {
    for(int i = 0; i < 9; i++) {
        if(g->b[i] != ' ') continue;
        g->b[i] = who;
        bool w = check(g->b) == who;
        g->b[i] = ' ';
        if(w) return i;
    }
    return -1;
}

static void cpu_move(G* g) {
    int m = find_completing(g, 'O');
    if(m < 0) m = find_completing(g, 'X');
    if(m < 0 && g->b[4] == ' ') m = 4;
    if(m < 0) {
        int free_cells[9], n = 0;
        for(int i = 0; i < 9; i++)
            if(g->b[i] == ' ') free_cells[n++] = i;
        if(n) m = free_cells[rand() % n];
    }
    if(m >= 0) g->b[m] = 'O';
}

static void finish(G* g) {
    g->winner = check(g->b);
    if(g->winner == 'X') g->wins++;
    if(g->winner == 'O') g->losses++;
    if(g->winner == 'D') g->draws++;
}

static void g_tick(G* g) {
    UNUSED(g);
}

static void g_input(G* g, InputEvent* e) {
    if(e->key == InputKeyOk && e->type == InputTypeShort) {
        if(g->winner) {
            g_reset(g);
            return;
        }
        if(g->b[g->cur] != ' ') return;
        g->b[g->cur] = 'X';
        finish(g);
        if(!g->winner) {
            cpu_move(g);
            finish(g);
        }
    } else if(is_step(e)) {
        int d = 0;
        if(e->key == InputKeyLeft) d = -1;
        if(e->key == InputKeyRight) d = 1;
        if(e->key == InputKeyUp) d = dpad ? -3 : -1;
        if(e->key == InputKeyDown) d = dpad ? 3 : 1;
        g->cur = (g->cur + d + 9) % 9;
    }
}

static void g_draw(Canvas* c, G* g) {
    const int ox = 4, oy = 8, s = 18;
    for(int i = 1; i < 3; i++) {
        canvas_draw_line(c, ox + i * s, oy, ox + i * s, oy + 3 * s);
        canvas_draw_line(c, ox, oy + i * s, ox + 3 * s, oy + i * s);
    }
    for(int i = 0; i < 9; i++) {
        int x = ox + (i % 3) * s, y = oy + (i / 3) * s;
        if(g->b[i] == 'X') {
            canvas_draw_line(c, x + 4, y + 4, x + s - 4, y + s - 4);
            canvas_draw_line(c, x + s - 4, y + 4, x + 4, y + s - 4);
        } else if(g->b[i] == 'O') {
            canvas_draw_circle(c, x + s / 2, y + s / 2, 6);
        }
        if(i == g->cur && !g->winner) canvas_draw_rframe(c, x + 1, y + 1, s - 1, s - 1, 2);
    }
    char b[32];
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 66, 12, "Tic-Tac-Toe");
    canvas_set_font(c, FontSecondary);
    snprintf(b, sizeof(b), "W %d  L %d  D %d", g->wins, g->losses, g->draws);
    canvas_draw_str(c, 66, 26, b);
    const char* msg = g->winner == 'X' ? "You win!" :
                      g->winner == 'O' ? "CPU wins" :
                      g->winner == 'D' ? "Draw" :
                                         "You are X";
    canvas_draw_str(c, 66, 40, msg);
    if(g->winner) canvas_draw_str(c, 66, 52, "OK = again");
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

int32_t tic_tac_toe_app(void* p) {
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
