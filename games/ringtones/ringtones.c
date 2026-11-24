// Ringtones: plays RTTTL tunes on the buzzer. Public-domain melodies only.
// Uses the notification service: FAPs cannot call furi_hal_speaker_start on this port.
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <stdlib.h>
#include <string.h>

static const char* const SONGS[][2] = {
    {"Ode to Joy", "d=4,o=5,b=140:e,e,f,g,g,f,e,d,c,c,d,e,e.,8d,2d,e,e,f,g,g,f,e,d,c,c,d,e,d.,8c,2c"},
    {"Fur Elise", "d=8,o=5,b=125:e6,d#6,e6,d#6,e6,b,d6,c6,4a,p,c,e,a,4b,p,e,g#,b,4c6,p,e,e6,d#6,e6,d#6,e6,b,d6,c6,4a"},
    {"Twinkle Twinkle", "d=4,o=5,b=120:c,c,g,g,a,a,2g,f,f,e,e,d,d,2c,g,g,f,f,e,e,2d,g,g,f,f,e,e,2d"},
    {"Happy Birthday", "d=4,o=5,b=125:8g.,16g,a,g,c6,2b,8g.,16g,a,g,d6,2c6,8g.,16g,g6,e6,c6,b,a,8f6.,16f6,e6,c6,d6,2c6"},
    {"Jingle Bells", "d=4,o=5,b=180:e,e,2e,e,e,2e,e,g,c.,8d,1e,f,f,f.,8f,f,e,e,8e,8e,e,d,d,e,2d,2g"},
    {"Mountain King", "d=8,o=5,b=160:a4,b4,c,d,e,c,4e,d#,b4,4d#,d,a#4,4d,a4,b4,c,d,e,c,e,a,g,e,c,e,2g"},
    {"Brahms Lullaby", "d=4,o=5,b=100:8e,8e,g.,8e,8e,g,p,8e,8g,c6,b.,8a,a,g,8d,8e,f,d,8d,8e,f,p,8d,8f,b,8a,8g,b,c6"},
    {"Morse SOS", "d=8,o=6,b=200:c,c,c,p,4c,4c,4c,p,c,c,c"},
};
#define SONG_COUNT COUNT_OF(SONGS)

typedef struct {
    FuriMutex* mutex;
    int sel, playing; // playing: song index or -1
    const char* pos;  // next note in the RTTTL string
    int def_d, def_o, bpm;
    uint32_t note_end;
} App;

static uint32_t now_ms(void) {
    return (uint32_t)((uint64_t)furi_get_tick() * 1000u / furi_kernel_get_tick_frequency());
}

static int num(const char** p) {
    int v = 0;
    while(**p >= '0' && **p <= '9') v = v * 10 + (*(*p)++ - '0');
    return v;
}

static void song_start(App* a, int i) {
    const char* p = SONGS[i][1];
    a->def_d = 4, a->def_o = 6, a->bpm = 63;
    while(*p && *p != ':') { // "d=4,o=5,b=140:"
        char k = *p;
        p += 2;
        int v = num(&p);
        if(k == 'd') a->def_d = v;
        if(k == 'o') a->def_o = v;
        if(k == 'b') a->bpm = v;
        if(*p == ',') p++;
    }
    a->pos = *p ? p + 1 : p;
    a->playing = i;
    a->note_end = 0;
}

// Parses the next note. Returns 0 at end of song; *freq 0 = rest.
static uint32_t next_note(App* a, float* freq) {
    static const float BASE[] = {261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,
                                 369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f}; // octave 4
    static const int8_t STEP[] = {9, 11, 0, 2, 4, 5, 7}; // a..g -> semitone from c
    const char* p = a->pos;
    while(*p == ' ' || *p == ',') p++;
    if(!*p) return 0;
    int d = num(&p);
    if(!d) d = a->def_d;
    int semi = -1;
    if(*p >= 'a' && *p <= 'g') semi = STEP[*p - 'a'];
    if(*p) p++;
    if(*p == '#') semi++, p++;
    bool dot = false;
    if(*p == '.') dot = true, p++;
    int o = num(&p);
    if(!o) o = a->def_o;
    if(*p == '.') dot = true, p++;
    a->pos = p;
    uint32_t ms = 240000u / (uint32_t)(a->bpm * d); // whole note = 4 beats
    if(dot) ms += ms / 2;
    *freq = semi < 0 ? 0 : BASE[semi % 12] * (float)(1 << (o - 4 + semi / 12));
    if(o < 4) *freq = semi < 0 ? 0 : BASE[semi % 12] / (float)(1 << (4 - o));
    return ms;
}

static void draw_cb(Canvas* c, void* ctx) {
    App* a = ctx;
    furi_mutex_acquire(a->mutex, FuriWaitForever);
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 10, a->playing >= 0 ? "Playing..." : "Ringtones");
    canvas_set_font(c, FontSecondary);
    int first = a->sel > 3 ? a->sel - 3 : 0;
    for(int i = first; i < (int)SONG_COUNT && i < first + 4; i++) {
        int y = 24 + (i - first) * 11;
        if(i == a->sel) {
            canvas_draw_rbox(c, 0, y - 9, 128, 11, 2);
            canvas_set_color(c, ColorWhite);
        }
        canvas_draw_str(c, 4, y, SONGS[i][0]);
        if(i == a->playing) canvas_draw_str(c, 118, y, ">");
        canvas_set_color(c, ColorBlack);
    }
    furi_mutex_release(a->mutex);
}

static void input_cb(InputEvent* e, void* ctx) {
    furi_message_queue_put(ctx, e, 0);
}

int32_t ringtones_app(void* p) {
    UNUSED(p);
    App* a = malloc(sizeof(App));
    memset(a, 0, sizeof(App));
    a->playing = -1;
    a->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    NotificationApp* notif = furi_record_open(RECORD_NOTIFICATION);
    // two alternating note sequences: the async notification queue holds a pointer
    // to the one in flight while the next is being filled
    NotificationMessage on[2], wait[2], off = {.type = NotificationMessageTypeSoundOff};
    const NotificationMessage* seq[2][4];
    int slot = 0;

    FuriMessageQueue* q = furi_message_queue_alloc(16, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_cb, a);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    for(bool run = true; run;) {
        InputEvent e;
        if(furi_message_queue_get(q, &e, furi_ms_to_ticks(10)) == FuriStatusOk) {
            furi_mutex_acquire(a->mutex, FuriWaitForever);
            bool step = e.type == InputTypePress || e.type == InputTypeRepeat;
            if(e.key == InputKeyBack && e.type == InputTypeShort) {
                if(a->playing >= 0) a->playing = -1; // first Back stops, second exits
                else run = false;
            } else if(e.key == InputKeyOk && e.type == InputTypeShort) {
                if(a->playing == a->sel) a->playing = -1;
                else song_start(a, a->sel);
            } else if(step && (e.key == InputKeyUp || e.key == InputKeyLeft)) {
                a->sel = (a->sel + SONG_COUNT - 1) % SONG_COUNT;
            } else if(step && (e.key == InputKeyDown || e.key == InputKeyRight)) {
                a->sel = (a->sel + 1) % SONG_COUNT;
            }
            furi_mutex_release(a->mutex);
            view_port_update(vp);
        }
        furi_mutex_acquire(a->mutex, FuriWaitForever);
        if(a->playing >= 0 && now_ms() >= a->note_end) {
            float f = 0;
            uint32_t ms = next_note(a, &f);
            if(!ms) {
                a->playing = -1;
                view_port_update(vp);
            } else {
                a->note_end = now_ms() + ms;
                slot ^= 1;
                on[slot] = (NotificationMessage){
                    .type = f > 0 ? NotificationMessageTypeSoundOn : NotificationMessageTypeSoundOff,
                    .data.sound = {.frequency = f, .volume = 1.0f}};
                wait[slot] = (NotificationMessage){
                    .type = NotificationMessageTypeDelay, .data.delay.length = ms * 9 / 10};
                seq[slot][0] = &on[slot];
                seq[slot][1] = &wait[slot];
                seq[slot][2] = &off; // short gap so repeated notes are distinct
                seq[slot][3] = NULL;
                notification_message(notif, (const NotificationSequence*)&seq[slot]);
            }
        }
        furi_mutex_release(a->mutex);
    }

    notification_message(notif, &(const NotificationSequence){&off, NULL});
    furi_delay_ms(50);
    view_port_enabled_set(vp, false);
    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_message_queue_free(q);
    furi_record_close(RECORD_NOTIFICATION);
    furi_mutex_free(a->mutex);
    free(a);
    return 0;
}
