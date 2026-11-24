// Mines -- part of the App Store game pack. Controls work on wheel boards
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
#define MW 16
#define MH 7
#define MINES 15
#define CELLS (MW * MH)

typedef struct {
    FuriMutex* mutex;
    bool mine[CELLS];
    uint8_t adj[CELLS], st[CELLS]; // st: 0 hidden, 1 open, 2 flag
    int cur, opened, flags;
    bool placed, over, won;
} G;

static void g_reset(G* g) {
    memset(g->mine, 0, sizeof(g->mine));
    memset(g->st, 0, sizeof(g->st));
    g->cur = CELLS / 2;
    g->opened = g->flags = 0;
    g->placed = g->over = g->won = false;
}

static void place(G* g, int safe) {
    for(int n = 0; n < MINES;) {
        int i = rand() % CELLS;
        int dx = i % MW - safe % MW, dy = i / MW - safe / MW;
        if(g->mine[i] || (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1)) continue; // first click opens an area
        g->mine[i] = true;
        n++;
    }
    for(int i = 0; i < CELLS; i++) {
        g->adj[i] = 0;
        for(int dy = -1; dy <= 1; dy++)
            for(int dx = -1; dx <= 1; dx++) {
                int x = i % MW + dx, y = i / MW + dy;
                if(x >= 0 && x < MW && y >= 0 && y < MH && g->mine[y * MW + x]) g->adj[i]++;
            }
    }
    g->placed = true;
}

static void reveal(G* g, int start) {
    int stack[CELLS], sp = 0; // a cell is marked open when pushed, so at most CELLS entries
    g->st[start] = 1;
    g->opened++;
    stack[sp++] = start;
    while(sp) {
        int i = stack[--sp];
        if(g->adj[i]) continue;
        for(int dy = -1; dy <= 1; dy++)
            for(int dx = -1; dx <= 1; dx++) {
                int x = i % MW + dx, y = i / MW + dy;
                if(x < 0 || x >= MW || y < 0 || y >= MH || g->st[y * MW + x] != 0) continue;
                g->st[y * MW + x] = 1;
                g->opened++;
                stack[sp++] = y * MW + x;
            }
    }
}

static void g_tick(G* g) {
    UNUSED(g);
}

static void g_input(G* g, InputEvent* e) {
    if(e->key == InputKeyOk && e->type == InputTypeShort) {
        if(g->over) {
            g_reset(g);
            return;
        }
        if(g->st[g->cur] != 0) return;
        if(!g->placed) place(g, g->cur);
        if(g->mine[g->cur]) {
            g->over = true;
            return;
        }
        reveal(g, g->cur);
        if(g->opened == CELLS - MINES) g->over = g->won = true;
    } else if(e->key == InputKeyOk && e->type == InputTypeLong && !g->over) {
        if(g->st[g->cur] == 0) g->st[g->cur] = 2, g->flags++;
        else if(g->st[g->cur] == 2) g->st[g->cur] = 0, g->flags--;
    } else if(is_step(e)) {
        int d = 0;
        if(e->key == InputKeyLeft) d = -1;
        if(e->key == InputKeyRight) d = 1;
        if(e->key == InputKeyUp) d = dpad ? -MW : -1; // wheel walks cell by cell
        if(e->key == InputKeyDown) d = dpad ? MW : 1;
        g->cur = (g->cur + d + CELLS) % CELLS;
    }
}

static void g_draw(Canvas* c, G* g) {
    char b[32];
    snprintf(b, sizeof(b), "Flags %d/%d  hold OK: flag", g->flags, MINES);
    canvas_draw_str(c, 1, 7, g->over ? (g->won ? "Cleared! OK = new" : "Boom! OK = new") : b);
    for(int i = 0; i < CELLS; i++) {
        int x = (i % MW) * 8, y = 8 + (i / MW) * 8;
        if(g->over && g->mine[i]) {
            canvas_draw_disc(c, x + 3, y + 3, 2);
        } else if(g->st[i] == 0) {
            canvas_draw_box(c, x, y, 7, 7);
        } else if(g->st[i] == 2) {
            canvas_draw_frame(c, x, y, 7, 7);
            canvas_draw_line(c, x + 2, y + 1, x + 2, y + 5);
            canvas_draw_box(c, x + 3, y + 1, 2, 2);
        } else if(g->adj[i]) {
            snprintf(b, sizeof(b), "%u", g->adj[i]);
            canvas_draw_str_aligned(c, x + 4, y + 4, AlignCenter, AlignCenter, b);
        }
    }
    canvas_set_color(c, ColorXOR);
    canvas_draw_frame(c, (g->cur % MW) * 8 - 1 < 0 ? 0 : (g->cur % MW) * 8 - 1, 7 + (g->cur / MW) * 8, 9, 9);
    canvas_set_color(c, ColorBlack);
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

int32_t mines_app(void* p) {
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
