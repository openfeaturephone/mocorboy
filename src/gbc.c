/* gbc.c - Game Boy / Game Boy Color emulator for the Ruio F286: the MMI app around gbc_core.c (Walnut-CGB).
 *
 * ROMs: .gb, .gbc or .cgb in E:\roms\ or D:\roms\. Saves sit beside them: <name>.sav is the cart's battery RAM, in the raw format other emulators
 * use, with <name>.sa1 written first as the copy that survives a power cut during the write.
 * Windows: a stock list to pick a game, a raw window for the game, a stock list over it for the menu.
 * Screen: the 160x144 picture scaled 3:2 to 240x216, rows 18..233, under the stock status bar. */
#include "f286_ui.h"
#include "app_rt.h"
#include "fw.h"
#include "gbc_core.h"

F286_APP_HEADER(f286_main);

#define WIN_PICK    0x00f28b00u
#define WIN_GAME    0x00f28b01u
#define WIN_MENU    0x00f28b02u
#define C_PICK      0x00f28b10u
#define C_MENU      0x00f28b11u

#define MSG_GET_FOCUS     0xf024u
#define KEY_LONG_RELEASED 0xfd00u

#define MAX_ROMS    64u
#define NAME_MAX    80u               /* UCS-2 units, with the terminator */
#define PATH_MAX    (NAME_MAX + 10u)
#define FIND_SIZE   0x400u
#define FIND_ATTR   0x14u
#define FIND_NAME   0x1cu
#define ATTR_DIR    0x10u
#define HEAP_KEEP   (1024u * 1024u)   /* left for the firmware */
#define MIN_SLOTS   16u               /* 256 KB */
#define GAME_TOP    18                /* below the stock status bar */
#define GAME_BOTTOM (GAME_TOP + GBC_SCREEN_H - 1)
#define BUDGET_MS   10u               /* stop emulating frames after this long in one tick */
#define MAX_BEHIND  5u                /* frames of debt kept; beyond this the game slows down instead */
#define MIN_DRAW_MS 66u               /* draw at least this often, however far behind */
#define AWAKE_MS    (5u * 60u * 1000u) /* with no key for this long the screen may sleep, which pauses the game */
#define WAKE_EVERY_MS 4000u           /* the stock backlight timeout can be as short as 10 s */
#define SAVE_QUIET_MS 3000u           /* autosave once the game has left cart RAM alone this long */

typedef struct { uint16_t name[NAME_MAX]; uint8_t drive; uint8_t pad; } rom_t;
typedef struct { uint32_t n; rom_t r[MAX_ROMS]; } rom_list_t;

enum { PH_NONE, PH_LOAD, PH_RUN, PH_DEAD };

typedef struct {
    uint32_t fmt[8];
    int16_t ring[16384];
    int16_t zero[512];
    int16_t frame[GBC_AUDIO_SAMPLES + 2u];
    uint8_t track[12000];
} aud_t;

typedef struct {
    const f286_api_t *api;
    rom_list_t *list;
    /* the running game: all of it is owned by the game window */
    gbc_t *g;
    uint8_t *arena, *ram;
    uint32_t h;                       /* ROM file, kept open only while paging on demand */
    uint32_t rom_pages, nslots, ram_size, load_page;
    uint32_t win;
    int phase, focused, in_frame, force_draw, smooth, show_stats;
    uint8_t timer, pad[3];
    uint32_t keys;                    /* physical keys held, one bit per key code */
    uint32_t last_ms, last_draw_ms, acc_us;
    uint32_t last_key_ms, last_wake_ms;
    /* sound; rd, given, t0 and pulls are written in the audio task */
    aud_t *aud;
    volatile uint32_t aud_h, aud_rd, aud_wr, aud_given, aud_t0, aud_pulls;
    uint32_t volume, st_pulls, aud_mode;
    uint16_t cfg[PATH_MAX + 4u], rtc[PATH_MAX + 4u];
    uint32_t seen_writes, change_ms, dirty;
    /* statistics, one window per second */
    uint32_t st_start, st_frames, st_drawn, st_busy;
    uint16_t path[PATH_MAX], save[PATH_MAX + 4u], back[PATH_MAX + 4u];
} state_t;
APP_STATE(state_t);
#define S (STATE())
#define API (STATE()->api)

/* ---- firmware calls that are not in API v3 (fw.h) ------------------------------- */
static uint32_t heap_free(void)
{
    return FW_A(0x880a95b8u, uint32_t (*)(uint32_t))(0x44444444u);
}
static uint32_t sfs_seek(uint32_t h, uint32_t off)
{
    /* SFS_SetFilePointer(h, pad, int64 in r2:r3, origin at [sp]), which is how AAPCS passes this prototype. Static
     * check only until the phone has run it (MMI_API_ADDRS.md). */
    return FW_T(0x8808664eu, uint32_t (*)(uint32_t, uint32_t, int64_t, uint32_t))(h, 0, (int64_t)off, 0);
}
static uint32_t find_first(const uint16_t *pattern, void *fd)
{
    return FW_T(0x88086754u, uint32_t (*)(const uint16_t *, void *))(pattern, fd);
}
static uint32_t find_next(uint32_t h, void *fd)
{
    return FW_T(0x88086a54u, uint32_t (*)(uint32_t, void *))(h, fd);
}
static void find_close(uint32_t h)
{
    FW_T(0x88086a92u, uint32_t (*)(uint32_t))(h);
}
static uint32_t total_seconds(void)
{
    return FW_A(0x880a97a8u, uint32_t (*)(void))();
}

/* ---- small helpers ------------------------------------------------------------------------------------------------ */
static uint32_t ws_copy(uint16_t *d, const uint16_t *s)
{
    uint32_t n = 0;
    while (s[n]) {
        d[n] = s[n];
        n++;
    }
    d[n] = 0;
    return n;
}
static uint16_t lower(uint16_t c) { return (c >= 'A' && c <= 'Z') ? (uint16_t)(c + 32) : c; }
static int ws_icmp(const uint16_t *a, const uint16_t *b)
{
    while (*a && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return (int)lower(*a) - (int)lower(*b);
}
static int is_rom_name(const uint16_t *n)
{
    uint32_t len = wlen(n), i = len;
    while (i && n[i - 1] != '.')
        i--;
    if (!i)
        return 0;
    return !ws_icmp(n + i, u"gb") || !ws_icmp(n + i, u"gbc") || !ws_icmp(n + i, u"cgb");
}

static void text(int x0, int x1, int y, const uint16_t *s, GUI_COLOR_T color)
{
    static const GUI_LCD_DEV_INFO dev = { 0, 0 };
    GUI_RECT_T r;
    MMI_STRING_T str;
    GUISTR_STYLE_T style;
    r.left = (int16_t)x0;
    r.top = (int16_t)y;
    r.right = (int16_t)x1;
    r.bottom = (int16_t)(y + 19);
    ui_str(&str, s);
    zero(&style, sizeof style);
    style.font = 16;
    style.font_color = color;
    style.align = 6;
    API->GUISTR_DrawTextToLCDInRect(&dev, &r, &r, &str, &style, 0, -1);
}

static uint16_t *get_fb(void)
{
    uint32_t pitch = 0, w = 0, h = 0;
    uint16_t *buf = 0;
    if (!fw_get_draw_param(API, &pitch, &w, &h, &buf) || !buf)
        return 0;
    if (pitch != 240u || w != 240u || h != 320u || fw_layer_type(API) != 3u)
        return 0;
    return buf;
}

static void fill_rows(uint16_t *fb, int y0, int y1, uint16_t c)
{
    for (uint16_t *p = fb + y0 * 240, *e = fb + (y1 + 1) * 240; p < e; p++)
        *p = c;
}

/* ---- saves --------------------------------------------------------------------------------------------------------- */
static int write_file(const uint16_t *path, const uint8_t *data, uint32_t len)
{
    uint32_t done = 0, h = API->SFS_CreateFile(path, SFS_MODE_CREATE_ALWAYS_WRITE, 0, 0);
    if (!h)
        return 0;
    API->SFS_WriteFile(h, data, len, &done, 0);
    API->SFS_CloseFile(h);
    return done == len;
}
static int read_save(const uint16_t *path)
{
    uint32_t size = 0, done = 0, h = API->SFS_CreateFile(path, SFS_MODE_READ_EXISTING, 0, 0);
    if (!h)
        return 0;
    /* longer is fine: some emulators append their clock */
    if (API->SFS_GetFileSize(h, &size) == 0 && size >= S->ram_size)
        API->SFS_ReadFile(h, S->ram, S->ram_size, &done, 0);
    API->SFS_CloseFile(h);
    return done == S->ram_size;
}
static void load_save(void)
{
    if (!S->ram_size)
        return;
    if (read_save(S->save) || read_save(S->back))
        return;
    for (uint32_t i = 0; i < S->ram_size; i++)
        S->ram[i] = 0xff;
}
/* ---- sound ----------------------------------------------------------------------------------------------------
 * The firmware pulls PCM through get_src, in its audio task, and fills its own buffer of about 1 s if it can, which would
 * make every sound 1 s late. So: prime that buffer with silence so playback starts, feed nothing while it drains, then feed
 * at the real rate to keep it nearly empty. The sequence before play is the firmware's own self-test (autotest_audio.c). */
#define AUD_RATE    GBC_AUDIO_RATE
#define AUD_RING    16384u            /* samples, a power of two */
#define AUD_PULL    4096u             /* samples handed over at most per pull. After "nothing yet" the firmware waits about
                                         100 ms (seen on the phone), so one pull has to be able to take all of that time's sound */
#define AUD_PRIME_PULL 512u           /* samples of silence per pull while priming */
#define AUD_PRIME   34560u            /* samples of silence, measured on the phone: it took about 1050 ms worth */
#define AUD_LOW     3600               /* estimated samples queued: below this, stretch a frame by two samples */
#define AUD_HIGH    5400               /* above this, drop the frame */
#define AUD_TRACK_UNITS 3000u
#define AUD_DEV_SPEAKER 1u
#define AUD_DEV_EARPHONE 2u
#define AUD_HEADSET_IN ((uint32_t (*)(void))0x880a9a28u)   /* what stock FM radio asks before "Headset is in" */
#define AUD_SET_MODE ((uint32_t (*)(uint32_t))0x880a9618u)
#define AUD_SET_VOL  ((uint32_t (*)(uint32_t))0x880a9610u)
#define AUD_PREPLAY  ((uint32_t (*)(uint32_t))0x880a9720u)
#define AUD_CREATE   ((uint32_t (*)(uint32_t, const void *, uint32_t, const void *, const void *, uint32_t, void *, uint32_t, \
                                    void *, void *))0x880a9408u)
#define AUD_PLAY     ((uint32_t (*)(uint32_t, uint32_t))0x880a9a88u)
#define AUD_STOP     ((uint32_t (*)(uint32_t))0x880a9a90u)
#define AUD_CLOSE    ((uint32_t (*)(uint32_t))0x880a9b08u)
#define AUD_CODEC_PCM (*(volatile const uint32_t *)0x81703cb0u)
#define AUD_DEV_OUT   (*(volatile const uint32_t *)0x81703d0cu)

static uint32_t aud_played(uint32_t ms)    /* samples the firmware has played after ms, without a 64-bit multiply */
{
    return ms / 1000u * AUD_RATE + ms % 1000u * AUD_RATE / 1000u;
}

/* Runs in the audio task: hand over a block and touch nothing else. info: [1] data, [2] bytes, [3] and [4] zero. */
static int aud_get_src(uint32_t h, uint32_t *info)
{
    aud_t *a = S->aud;
    uint32_t rd, avail, n;
    (void)h;
    if (!a || !S->aud_h)
        return 3;
    if (!S->aud_t0)
        S->aud_t0 = FW_MS_NOW() | 1u;
    S->aud_pulls++;
    info[3] = 0;
    info[4] = 0;
    if (S->aud_given < AUD_PRIME) {
        info[1] = (uint32_t)a->zero;
        info[2] = AUD_PRIME_PULL * 2u;
        S->aud_given += AUD_PRIME_PULL;
        return 0;
    }
    rd = S->aud_rd;
    avail = S->aud_wr - rd;
    if (!avail)
        return 1;                      /* nothing yet: the stock decoders answer the same way */
    n = AUD_RING - (rd & (AUD_RING - 1u));
    if (n > avail)
        n = avail;
    if (n > AUD_PULL)
        n = AUD_PULL;
    info[1] = (uint32_t)&a->ring[rd & (AUD_RING - 1u)];
    info[2] = n * 2u;
    S->aud_given += n;
    __asm__ volatile("dmb");
    S->aud_rd = rd + n;
    return 0;
}

static void aud_notify(void) {}

static void aud_close(void)
{
    uint32_t h = S->aud_h;
    if (h) {
        S->aud_h = 0;
        AUD_STOP(h);
        AUD_CLOSE(h);
    }
}

/* Follow the headphone jack, as stock audio does. Called when a stream opens and once a second. */
static void aud_route(void)
{
    uint32_t mode = AUD_HEADSET_IN() ? AUD_DEV_EARPHONE : AUD_DEV_SPEAKER;
    if (mode != S->aud_mode) {
        S->aud_mode = mode;
        AUD_SET_MODE(mode);
    }
}

static void aud_open(void)
{
    uint8_t *f;
    aud_t *a;
    if (S->aud_h || !S->volume || S->phase != PH_RUN)
        return;
    if (!S->aud)
        S->aud = API->alloc(sizeof(aud_t));
    a = S->aud;
    if (!a)
        return;                        /* the game still runs, silently */
    zero(a, sizeof *a);
    S->aud_rd = S->aud_wr = S->aud_given = S->aud_t0 = S->aud_pulls = 0;
    f = (uint8_t *)a->fmt;
    f[0] = 1;
    a->fmt[1] = 1;
    a->fmt[2] = AUD_RATE;
    a->fmt[3] = 16;
    *(uint16_t *)(f + 0x10) = 1;       /* mono */
    a->fmt[5] = AUD_PRIME_PULL * 2u;
    a->fmt[6] = 2;
    S->aud_mode = 0;
    aud_route();
    AUD_SET_VOL(S->volume);
    S->aud_h = AUD_CREATE(AUD_CODEC_PCM, 0, AUD_DEV_OUT, 0, a->fmt, 0x20u, a->track, AUD_TRACK_UNITS, (void *)aud_notify,
                          (void *)aud_get_src);
    if (S->aud_h) {
        AUD_PREPLAY(1);
        if (AUD_PLAY(S->aud_h, 0) != 0)
            aud_close();
    }
}

static int32_t aud_ahead(uint32_t now)
{
    uint32_t queued = S->aud_given + (S->aud_wr - S->aud_rd);
    return (int32_t)(queued - aud_played(now - (S->aud_t0 & ~1u)));
}

/* One emulated frame's sound into the ring. The APU always runs, so it stays in time with the game. */
static void aud_push(void)
{
    aud_t *a = S->aud;
    uint32_t wr, n = GBC_AUDIO_SAMPLES, i;
    int32_t ahead;
    if (!S->aud_h)
        return;
    gbc_audio_frame(S->g, a->frame);
    if (!S->aud_t0 || S->aud_given < AUD_PRIME)
        return;
    ahead = aud_ahead(FW_MS_NOW());
    if (ahead >= AUD_HIGH)
        return;
    if (ahead < AUD_LOW) {
        a->frame[GBC_AUDIO_SAMPLES] = a->frame[GBC_AUDIO_SAMPLES + 1u] = a->frame[GBC_AUDIO_SAMPLES - 1u];
        n += 2u;
    }
    wr = S->aud_wr;
    if (ahead <= 0 && n + AUD_LOW + AUD_PULL <= AUD_RING - (wr - S->aud_rd)) {
        /* It ran dry (a save, a slow frame). Silence rebuilds the cushion in one go, rather than every frame after being late. */
        for (i = 0; i < AUD_LOW; i++)
            a->ring[(wr + i) & (AUD_RING - 1u)] = 0;
        wr += AUD_LOW;
    }
    if (n + AUD_PULL > AUD_RING - (wr - S->aud_rd))
        return;
    for (i = 0; i < n; i++)
        a->ring[(wr + i) & (AUD_RING - 1u)] = a->frame[i];
    __asm__ volatile("dmb");
    S->aud_wr = wr + n;
}

static const uint16_t *sound_label(void)
{
    switch (S->volume) {
    case 0: return u"Sound: off";
    case 3: return u"Sound: low";
    case 6: return u"Sound: medium";
    default: return u"Sound: high";
    }
}

/* gbc.cfg beside the ROMs: one byte, the volume */
static void load_cfg(void)
{
    uint32_t done = 0, h = API->SFS_CreateFile(S->cfg, SFS_MODE_READ_EXISTING, 0, 0);
    uint8_t v = 0;
    if (!h)
        return;
    API->SFS_ReadFile(h, &v, 1, &done, 0);
    API->SFS_CloseFile(h);
    if (done == 1 && v <= 9u)
        S->volume = v;
}

/* ---- the cartridge clock ----------------------------------------------------------------------------------------
 * <name>.rtc: "GRT1", the five MBC3 registers, three zeros, the phone's seconds at that moment. Like the battery on a real
 * cart, the time passed while the game was off is added on load. */
static int load_rtc(uint32_t secs)
{
    uint8_t b[16];
    uint32_t done = 0, then, h = API->SFS_CreateFile(S->rtc, SFS_MODE_READ_EXISTING, 0, 0);
    if (!h)
        return 0;
    API->SFS_ReadFile(h, b, 16, &done, 0);
    API->SFS_CloseFile(h);
    if (done != 16 || b[0] != 'G' || b[1] != 'R' || b[2] != 'T' || b[3] != '1')
        return 0;
    then = b[12] | (uint32_t)b[13] << 8 | (uint32_t)b[14] << 16 | (uint32_t)b[15] << 24;
    gbc_put_rtc(S->g, b + 4, secs > then ? secs - then : 0u);
    return 1;
}

static void save_rtc(void)
{
    uint8_t b[16];
    uint32_t secs;
    if (!S->g || !gbc_has_rtc(S->g))
        return;
    secs = total_seconds();
    zero(b, sizeof b);
    b[0] = 'G'; b[1] = 'R'; b[2] = 'T'; b[3] = '1';
    gbc_get_rtc(S->g, b + 4);
    b[12] = (uint8_t)secs; b[13] = (uint8_t)(secs >> 8); b[14] = (uint8_t)(secs >> 16); b[15] = (uint8_t)(secs >> 24);
    write_file(S->rtc, b, sizeof b);
}

/* The backup goes first: at any moment one of the two files is whole. */
static int save_game(void)
{
    int ok;
    save_rtc();
    if (!S->ram_size)
        return 1;
    ok = write_file(S->back, S->ram, S->ram_size);
    ok = write_file(S->save, S->ram, S->ram_size) && ok;
    if (ok)
        S->dirty = 0;
    return ok;
}

/* ---- ROM pages ------------------------------------------------------------------------------------------------------ */
static uint32_t read_page(void *user, uint32_t page, uint8_t *dst)
{
    uint32_t done = 0;
    (void)user;
    if (!S->h || sfs_seek(S->h, page * GBC_PAGE_SIZE) != 0)
        return 0;
    API->SFS_ReadFile(S->h, dst, GBC_PAGE_SIZE, &done, 0);
    return done;
}

/* ---- the game window ------------------------------------------------------------------------------------------------ */
static void arm(uint32_t ms)
{
    if (S->timer)
        API->MMK_StopTimer(S->timer);
    S->timer = API->MMK_CreateWinTimer(S->win, ms, 0, 0, 0);
}

static void paint_chrome(void)
{
    uint16_t *fb = get_fb();
    if (!fb)
        return;
    fill_rows(fb, 0, GAME_TOP - 1, 0);
    fill_rows(fb, GAME_BOTTOM + 1, 319, 0);
    if (S->phase != PH_RUN)
        fill_rows(fb, GAME_TOP, GAME_BOTTOM, 0);
    fw_store_update_rect(API, 0, 0, 239, GAME_TOP - 1);
    fw_store_update_rect(API, 0, GAME_BOTTOM + 1, 239, 319);
    text(4, 125, 238, u"A: OK 5 3 9", 0xc618);
    text(126, 239, 238, u"B: Green 1 7", 0xc618);
    text(4, 125, 258, u"Start: LSoft *", 0xc618);
    text(126, 239, 258, u"Select: # 0", 0xc618);
    text(4, 172, 278, u"Move: pad 2 4 6 8", 0xc618);
    text(4, 125, 298, u"RSoft: Menu", 0xffff);
    if (S->phase == PH_LOAD)
        text(70, 200, 110, u"Loading...", 0xffff);
    S->force_draw = 1;
}

static uint32_t button_of(uint32_t key)
{
    switch (key) {
    case KEY_UP: case KEY_2: return GBC_UP;
    case KEY_DOWN: case KEY_8: return GBC_DOWN;
    case KEY_LEFT: case KEY_4: return GBC_LEFT;
    case KEY_RIGHT: case KEY_6: return GBC_RIGHT;
    case KEY_OK: case KEY_5: case KEY_3: case KEY_9: return GBC_A;
    case KEY_GREEN: case KEY_1: case KEY_7: return GBC_B;
    case KEY_LSOFT: case KEY_STAR: return GBC_START;
    case KEY_HASH: case KEY_0: return GBC_SELECT;
    default: return 0;
    }
}
static void apply_keys(void)
{
    uint32_t b = 0, k = S->keys;
    for (uint32_t i = 0; k; i++, k >>= 1)
        if (k & 1u)
            b |= button_of(i);
    if (S->phase == PH_RUN)
        gbc_set_buttons(S->g, b);
}

static uint32_t decimal(uint16_t *d, uint32_t v)
{
    uint16_t t[10];
    uint32_t n = 0, i = 0;
    do {
        t[n++] = (uint16_t)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        d[i++] = t[--n];
    return i;
}

static void draw_stats(uint32_t interval)
{
    static const GUI_LCD_DEV_INFO dev = { 0, 0 };
    uint16_t line[24];
    uint32_t n = 0;
    GUI_RECT_T r;
    r.left = 106; r.top = 298; r.right = 239; r.bottom = 317;
    API->GUI_FillRect(&dev, r, 0);
    n += decimal(line + n, S->st_drawn * 1000u / interval);
    line[n++] = 'f';
    line[n++] = ' ';
    n += decimal(line + n, S->st_frames * GBC_FRAME_US / interval / 10u);
    line[n++] = '%';
    line[n++] = ' ';
    n += decimal(line + n, S->st_busy * 100u / interval);
    line[n++] = 'c';
    line[n] = 0;
    text(106, 239, 298, line, 0x07e0);
    /* sound: estimated ms queued, and pulls per second. No pulls means the firmware stopped asking. */
    r.left = 174; r.top = 278; r.bottom = 297;
    API->GUI_FillRect(&dev, r, 0);
    if (S->aud_h && S->aud_t0) {
        int32_t a = aud_ahead(FW_MS_NOW());
        n = 0;
        line[n++] = 'q';
        n += decimal(line + n, a > 0 ? (uint32_t)a / 33u : 0u);
        line[n++] = ' ';
        line[n++] = 'p';
        n += decimal(line + n, (S->aud_pulls - S->st_pulls) * 1000u / interval);
        line[n] = 0;
        text(174, 239, 278, line, 0x07e0);
        S->st_pulls = S->aud_pulls;
    }
}

/* After this the window may be gone: callers must return without touching game state. */
static void fail_game(uint32_t win, const uint16_t *line1, const uint16_t *line2)
{
    S->phase = PH_DEAD;
    API->MMK_CloseWin(win);
    ui_alert(API, line1, line2);
}

static void start_running(uint32_t win)
{
    uint32_t secs = total_seconds();
    int e;

    S->last_key_ms = FW_MS_NOW();

    if (S->rom_pages <= S->nslots && S->h) {   /* everything is in memory */
        API->SFS_CloseFile(S->h);
        S->h = 0;
    }
    load_save();
    e = gbc_start(S->g);
    if (e != GBC_OK) {
        fail_game(win, u"Cannot run this game", u"Unsupported cartridge");
        return;
    }
    if (!load_rtc(secs))
        gbc_set_rtc(S->g, secs % 60u, secs / 60u % 60u, secs / 3600u % 24u, secs / 86400u);
    gbc_set_smooth(S->g, S->smooth);
    S->phase = PH_RUN;
    S->seen_writes = 0;
    S->dirty = 0;
    S->last_ms = S->last_draw_ms = S->st_start = FW_MS_NOW();
    S->acc_us = 0;
    S->st_frames = S->st_drawn = S->st_busy = 0;
    paint_chrome();
    aud_open();
    arm(1);
}

static void load_tick(uint32_t win)
{
    uint32_t t0 = FW_MS_NOW();
    do {
        uint8_t *slot;
        uint32_t done = 0;
        if (S->load_page >= S->rom_pages || !(slot = gbc_prefill_slot(S->g, S->load_page))) {
            start_running(win);
            return;
        }
        /* sequential, so no seek: the file is at this page already */
        API->SFS_ReadFile(S->h, slot, GBC_PAGE_SIZE, &done, 0);
        if (done == 0) {
            fail_game(win, u"Cannot read the game", 0);
            return;
        }
        while (done < GBC_PAGE_SIZE)
            slot[done++] = 0xff;
        S->load_page++;
    } while (FW_MS_NOW() - t0 < BUDGET_MS);
    arm(1);
}

/* Cutscenes and title screens run with no keys pressed. MMIDEFAULT_TurnOnBackLight restarts the backlight timeout. */
static void keep_screen_on(uint32_t now)
{
    if (now - S->last_key_ms < AWAKE_MS && now - S->last_wake_ms >= WAKE_EVERY_MS) {
        S->last_wake_ms = now;
        FW_T(0x882583e2u, void (*)(void))();
    }
}

static void run_tick(uint32_t win)
{
    uint32_t now = FW_MS_NOW(), t, w, next;
    uint16_t *fb;
    int drew = 0;

    t = now - S->last_ms;
    S->last_ms = now;
    if (t > 200u)                      /* a long gap is a stall, not time to catch up on */
        t = 200u;
    S->acc_us += t * 1000u;
    if (S->acc_us > MAX_BEHIND * GBC_FRAME_US)
        S->acc_us = MAX_BEHIND * GBC_FRAME_US;

    fb = get_fb();
    gbc_set_fb(S->g, fb ? fb + GAME_TOP * 240 : 0, 240);
    S->in_frame = 1;
    while (S->acc_us >= GBC_FRAME_US) {
        int draw;
        S->acc_us -= GBC_FRAME_US;
        draw = fb && (S->acc_us < GBC_FRAME_US || S->force_draw || FW_MS_NOW() - S->last_draw_ms >= MIN_DRAW_MS);
        gbc_run_frame(S->g, draw);
        aud_push();
        S->st_frames++;
        if (draw) {
            drew = 1;
            S->force_draw = 0;
            S->last_draw_ms = FW_MS_NOW();
            S->st_drawn++;
        }
        if (gbc_error(S->g) || FW_MS_NOW() - now >= BUDGET_MS)
            break;
    }
    S->in_frame = 0;
    gbc_set_fb(S->g, 0, 0);            /* the pointer is good for this message only */
    if (drew)
        fw_store_update_rect(API, 0, GAME_TOP, 239, GAME_BOTTOM);

    if (gbc_error(S->g)) {
        if (S->dirty || gbc_ram_writes(S->g) != S->seen_writes)
            save_game();
        fail_game(win, gbc_error(S->g) == GBC_ERR_ROMIO ? u"Cannot read the game" : u"The game crashed", 0);
        return;
    }

    /* autosave once cart RAM has gone quiet */
    w = gbc_ram_writes(S->g);
    t = FW_MS_NOW();
    if (w != S->seen_writes) {
        S->seen_writes = w;
        S->change_ms = t;
        S->dirty = 1;
    } else if (S->dirty && t - S->change_ms >= SAVE_QUIET_MS) {
        save_game();                   /* on failure: dirty stays, and it tries again after the next change or on exit */
        S->change_ms = t;
        t = FW_MS_NOW();
    }

    S->st_busy += t - now;
    if (t - S->st_start >= 1000u) {
        if (S->aud_h)
            aud_route();
        if (S->show_stats)
            draw_stats(t - S->st_start);
        S->st_start = t;
        S->st_frames = S->st_drawn = S->st_busy = 0;
    }

    next = S->acc_us >= GBC_FRAME_US ? 1u : (GBC_FRAME_US - S->acc_us) / 1000u;
    arm(next ? next : 1u);
}

/* ---- menu over the game --------------------------------------------------------------------------------------------- */
static uint32_t menu_win(uint32_t win, uint32_t msg, void *param)
{
    (void)param;
    switch (msg) {
    case MSG_OPEN_WINDOW:
        ui_list_begin(API, C_MENU, 7);
        ui_list_add(API, C_MENU, u"Resume", F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_add(API, C_MENU, u"Save now", F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_add(API, C_MENU, u"Restart game", F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_add(API, C_MENU, sound_label(), F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_add(API, C_MENU, S->smooth ? u"Smoothing: on" : u"Smoothing: off", F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_add(API, C_MENU, S->show_stats ? u"Speed display: on" : u"Speed display: off", F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_add(API, C_MENU, u"Exit game", F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_end(API, win, C_MENU);
        return 1;
    case MSG_APP_OK:
    case MSG_APP_WEB: {
        uint32_t row = API->GUILIST_GetCurItemIndex(C_MENU);
        API->MMK_CloseWin(win);
        if (S->phase != PH_RUN)
            return 1;
        switch (row) {
        case 1:
            if (save_game())
                ui_alert(API, u"Saved", 0);
            else
                ui_alert(API, u"Could not save", u"Is the card full?");
            break;
        case 2:
            if (S->dirty)
                save_game();
            gbc_reset(S->g);
            break;
        case 3: {
            uint8_t v;
            S->volume = S->volume >= 9u ? 0u : S->volume + 3u;
            v = (uint8_t)S->volume;
            write_file(S->cfg, &v, 1);
            break;
        }
        case 4:
            S->smooth = !S->smooth;
            gbc_set_smooth(S->g, S->smooth);
            S->force_draw = 1;
            break;
        case 5:
            S->show_stats = !S->show_stats;
            break;
        case 6:
            API->MMK_CloseWin(S->win);
            break;
        default:
            break;
        }
        return 1;
    }
    case MSG_APP_CANCEL:
        API->MMK_CloseWin(win);
        return 1;
    default:
        return 0;
    }
}

static void open_menu(void)
{
    ui_t t;
    ui_begin(&t, menu_win, WIN_MENU);
    ui_softkey(&t, F286_TXT_SELECT, F286_TXT_NULL, F286_TXT_BACK);
    ui_title(&t, F286_TXT_MENU);
    ui_list(&t, C_MENU);
    ui_open(API, &t);
}

static void regain_focus(void)
{
    if (S->focused)
        return;
    S->focused = 1;
    S->last_ms = FW_MS_NOW();
    S->acc_us = 0;
    aud_open();
    arm(1);
}

static void free_game(void)
{
    aud_close();
    if (S->timer) {
        API->MMK_StopTimer(S->timer);
        S->timer = 0;
    }
    if (S->h) {
        API->SFS_CloseFile(S->h);
        S->h = 0;
    }
    if (S->g) {
        gbc_t *g = S->g;
        S->g = 0;
        API->free(g);
    }
    if (S->arena) {
        uint8_t *a = S->arena;
        S->arena = 0;
        API->free(a);
    }
    if (S->ram) {
        uint8_t *r = S->ram;
        S->ram = 0;
        API->free(r);
    }
    if (S->aud) {
        aud_t *a = S->aud;
        S->aud = 0;
        API->free(a);
    }
    S->phase = PH_NONE;
}

static uint32_t game_win(uint32_t win, uint32_t msg, void *param)
{
    uint32_t cls = msg & 0xff00u, key = msg & 0xffu;

    switch (msg) {
    case MSG_OPEN_WINDOW:
        S->win = win;
        S->focused = 1;
        arm(1);
        return 1;
    case MSG_FULL_PAINT:
        if (S->in_frame)               /* esd_TASK can send this while a frame is being drawn; the next tick repaints */
            return 1;
        paint_chrome();
        regain_focus();
        return 1;
    case MSG_GET_FOCUS:
        regain_focus();
        return 1;
    case MSG_LOSE_FOCUS:
        S->focused = 0;
        aud_close();
        if (S->timer) {
            API->MMK_StopTimer(S->timer);
            S->timer = 0;
        }
        S->keys = 0;
        apply_keys();
        return 1;
    case MSG_TIMER:
        if (*(uint8_t *)param != S->timer)
            return 0;
        S->timer = 0;
        if (!S->focused)
            return 1;
        if (S->phase == PH_LOAD) {
            load_tick(win);
        } else if (S->phase == PH_RUN) {
            /* screen off: no one is playing, and the game must not run on unseen */
            if (fw_backlight_state() == 2u) {
                aud_close();
                S->last_ms = FW_MS_NOW();
                arm(200);
            } else {
                keep_screen_on(FW_MS_NOW());
                aud_open();
                run_tick(win);
            }
        }
        return 1;
    case MSG_CLOSE_WINDOW:
        if (S->phase == PH_RUN) {
            if (S->dirty || gbc_ram_writes(S->g) != S->seen_writes)
                save_game();
            else
                save_rtc();
        }
        free_game();
        return 1;
    case MSG_APP_OK:
    case MSG_APP_WEB:
    case MSG_APP_CANCEL:
        return 1;
    default:
        break;
    }
    if (cls == KEY_PRESSED) {
        S->last_key_ms = FW_MS_NOW();
        if (key == KEY_RED) {
            API->MMK_CloseWin(win);
        } else if (key == KEY_RSOFT) {
            if (S->phase == PH_RUN)
                open_menu();
            else
                API->MMK_CloseWin(win);
        } else if (key < 32u) {
            S->keys |= 1u << key;
            apply_keys();
        }
        return 1;
    }
    if (cls == KEY_RELEASED || cls == KEY_LONG_RELEASED) {
        if (key < 32u) {
            S->keys &= ~(1u << key);
            apply_keys();
        }
        return 1;
    }
    if (cls == KEY_LONG_PRESSED || cls == 0xfe00u)
        return 1;
    return 0;
}

/* ---- opening a game -------------------------------------------------------------------------------------------------- */
static void set_paths(const rom_t *r)
{
    uint32_t n, dot;
    S->path[0] = r->drive;
    n = ws_copy(S->path + 1, u":\\roms\\") + 1u;
    n += ws_copy(S->path + n, r->name);
    dot = n;
    while (dot && S->path[dot - 1] != '.')
        dot--;
    ws_copy(S->save, S->path);
    ws_copy(S->save + dot, u"sav");
    ws_copy(S->back, S->path);
    ws_copy(S->back + dot, u"sa1");
    ws_copy(S->rtc, S->path);
    ws_copy(S->rtc + dot, u"rtc");
    S->cfg[0] = r->drive;
    ws_copy(S->cfg + 1, u":\\roms\\gbc.cfg");
    load_cfg();
}

static const uint16_t *check_message(int e)
{
    switch (e) {
    case GBC_E_CHECKSUM: return u"Bad ROM checksum";
    case GBC_E_CART: return u"Unsupported cartridge";
    default: return u"Not a Game Boy ROM";
    }
}

static void open_game(const rom_t *r)
{
    uint8_t hdr[0x150];
    uint32_t size = 0, done = 0, avail, want, nslots, ram_size = 0, ctx = gbc_ctx_size();
    MMI_WINDOW_CREATE_T wc;
    gbc_t *g;
    int e;

    set_paths(r);
    S->h = API->SFS_CreateFile(S->path, SFS_MODE_READ_EXISTING, 0, 0);
    if (!S->h) {
        ui_alert(API, u"Cannot open the game", 0);
        return;
    }
    API->SFS_GetFileSize(S->h, &size);
    API->SFS_ReadFile(S->h, hdr, sizeof hdr, &done, 0);
    e = done == sizeof hdr ? gbc_check_header(hdr, size, &ram_size) : GBC_E_HEADER;
    if (e != GBC_OK) {
        free_game();
        ui_alert(API, u"Cannot run this game", check_message(e));
        return;
    }
    if (sfs_seek(S->h, 0) != 0) {
        free_game();
        ui_alert(API, u"Cannot read the game", 0);
        return;
    }
    S->rom_pages = (size + GBC_PAGE_SIZE - 1u) / GBC_PAGE_SIZE;
    S->ram_size = ram_size;
    S->load_page = 0;

    S->g = API->alloc(ctx);
    S->ram = API->alloc(ram_size + 4u);
    g = S->g;
    avail = heap_free();
    want = avail > HEAP_KEEP + (MIN_SLOTS * GBC_PAGE_SIZE) ? (avail - HEAP_KEEP) / GBC_PAGE_SIZE : MIN_SLOTS;
    nslots = want < S->rom_pages ? want : S->rom_pages;
    if (nslots < 4u)
        nslots = 4u;                   /* a 32 KB game has two pages; the core may still ask for aliases */
    while (g && S->ram && !S->arena && nslots >= 4u) {
        S->arena = API->alloc(nslots * GBC_PAGE_SIZE);
        if (!S->arena)
            nslots = nslots > 16u ? nslots - 16u : nslots - 1u;
    }
    if (!g || !S->ram || !S->arena) {
        free_game();
        ui_alert(API, u"Not enough memory", 0);
        return;
    }
    S->nslots = nslots;
    gbc_setup(g, S->arena, nslots, S->rom_pages, read_page, 0, S->ram, ram_size);

    S->phase = PH_LOAD;
    S->in_frame = 0;
    S->keys = 0;
    zero(&wc, sizeof wc);
    wc.applet = API->MMK_GetCurApplet();
    wc.win_id = WIN_GAME;
    wc.func = (void *)game_win;
    wc.priority = 1;
    API->MMK_CreateWindow(&wc);
}

/* ---- picking a game --------------------------------------------------------------------------------------------------- */
static void scan_dir(rom_list_t *l, uint8_t *fd, uint16_t drive)
{
    uint16_t pattern[12];
    uint32_t h, n = 0;

    pattern[0] = drive;
    ws_copy(pattern + 1, u":\\roms\\*");
    h = find_first(pattern, fd);
    if (h == 0 || h == 0xffffffffu)
        return;
    do {
        const uint16_t *name = (const uint16_t *)(fd + FIND_NAME);
        uint32_t attr = fd[FIND_ATTR] | (uint32_t)fd[FIND_ATTR + 1] << 8;
        if (!(attr & ATTR_DIR) && wlen(name) < NAME_MAX && is_rom_name(name) && l->n < MAX_ROMS) {
            uint32_t i = l->n++;
            /* insertion sort: a short list, and find gives no order */
            while (i && ws_icmp(l->r[i - 1].name, name) > 0) {
                l->r[i] = l->r[i - 1];
                i--;
            }
            ws_copy(l->r[i].name, name);
            l->r[i].drive = (uint8_t)drive;
        }
    } while (++n < 4096u && find_next(h, fd) == 0);
    find_close(h);
}

static uint32_t pick_win(uint32_t win, uint32_t msg, void *param)
{
    (void)param;
    switch (msg) {
    case MSG_OPEN_WINDOW:
        ui_list_begin(API, C_PICK, S->list->n);
        for (uint32_t i = 0; i < S->list->n; i++)
            ui_list_add(API, C_PICK, S->list->r[i].name, F286_TXT_SELECT, 0, F286_TXT_BACK);
        ui_list_end(API, win, C_PICK);
        return 1;
    case MSG_APP_OK:
    case MSG_APP_WEB:
        if (!API->MMK_IsOpenWin(WIN_GAME))
            open_game(&S->list->r[API->GUILIST_GetCurItemIndex(C_PICK)]);
        return 1;
    case MSG_APP_CANCEL:
        API->MMK_CloseWin(win);
        return 1;
    case MSG_CLOSE_WINDOW:
        if (S->list) {
            rom_list_t *l = S->list;
            S->list = 0;
            API->free(l);
        }
        return 0;
    default:
        return 0;
    }
}

void f286_main(const f286_api_t *api, void *base, uint32_t size)
{
    ui_t t;
    uint8_t *fd;
    (void)base; (void)size;
    if (api->version < 3 || api->MMK_IsOpenWin(WIN_PICK))
        return;
    zero(S, sizeof *S);
    S->api = api;
    S->smooth = 1;
    S->volume = 6;
    S->list = api->alloc(sizeof(rom_list_t));
    fd = api->alloc(FIND_SIZE);
    if (!S->list || !fd) {
        if (S->list)
            api->free(S->list);
        if (fd)
            api->free(fd);
        S->list = 0;
        ui_alert(api, u"Not enough memory", 0);
        return;
    }
    S->list->n = 0;
    scan_dir(S->list, fd, 'E');
    scan_dir(S->list, fd, 'D');
    api->free(fd);
    if (!S->list->n) {
        api->free(S->list);
        S->list = 0;
        ui_alert(api, u"No games found", u"Put ROMs in E:\\gb");
        return;
    }
    ui_begin(&t, pick_win, WIN_PICK);
    ui_softkey(&t, F286_TXT_SELECT, F286_TXT_NULL, F286_TXT_BACK);
    ui_title(&t, F286_TXT_MENU);
    ui_list(&t, C_PICK);
    ui_open(api, &t);
}
