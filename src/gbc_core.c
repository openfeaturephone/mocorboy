/* gbc_core.c - see gbc_core.h. Keep this file free of firmware calls. */
#include "gbc_core.h"

/* Sound is minigb_apu, in this structure with no globals; gbc.c streams its output. The core's own default of 32-bit DMA with aligned reads is what the phone wants: the kernel faults on
 * unaligned access. */
#define ENABLE_SOUND 1
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#define AUDIO_SAMPLE_RATE GBC_AUDIO_RATE
#define ENABLE_LCD 1
#define WALNUT_GB_12_COLOUR 1
#define WALNUT_FULL_GBC_SUPPORT 1
/* Upstream marks gb_error as never returning and follows it with __builtin_unreachable. Here it returns, so that path
 * must be a plain return; error_cb ends the frame. */
#define WGB_UNREACHABLE() ((void)0)

/* the header first names this inside a prototype, which would give it prototype scope */
struct gb_s;

/* The core calls these as plain functions, always with its own `gb` in scope: that is how they find the APU without a global */
static uint8_t core_audio_read(struct gb_s *gb, uint16_t addr);
static void core_audio_write(struct gb_s *gb, uint16_t addr, uint8_t val);
#define audio_read(addr) core_audio_read((struct gb_s *)gb, (addr))
#define audio_write(addr, val) core_audio_write((struct gb_s *)gb, (addr), (val))

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wmacro-redefined"
#include "walnut_cgb/walnut_cgb.h"
#include "minigb_apu/minigb_apu.c"
#pragma clang diagnostic pop

struct gbc {
    struct gb_s gb;                     /* first: callbacks get this pointer */
    uint8_t *pages[GBC_MAX_PAGES];      /* page -> resident data, NULL = not loaded; aliases share a slot */
    uint8_t *slots;
    uint32_t nslots, used, clock, rom_pages;
    gbc_read_page_fn rd;
    void *user;
    uint8_t *ram;
    uint32_t ram_size, ram_writes, faults;
    uint16_t *fb;
    uint32_t pitch;
    int smooth, err;
    uint32_t err_addr;
    uint16_t dmg_pal[12];               /* OBJ0, OBJ1, BG; four shades each */
    struct minigb_apu_ctx apu;
    audio_sample_t apu_out[GBC_AUDIO_SAMPLES * 2u];
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-folding-constant"
_Static_assert(AUDIO_SAMPLES == GBC_AUDIO_SAMPLES, "GBC_AUDIO_SAMPLES must match the APU");
#pragma clang diagnostic pop

static uint8_t core_audio_read(struct gb_s *gb, uint16_t addr)
{
    return minigb_apu_audio_read(&((gbc_t *)gb)->apu, addr);
}

static void core_audio_write(struct gb_s *gb, uint16_t addr, uint8_t val)
{
    minigb_apu_audio_write(&((gbc_t *)gb)->apu, addr, val);
}

/* ---- ROM paging ------------------------------------------------------------------------------------------------ */
static uint8_t *page_fault(gbc_t *g, uint32_t page)
{
    uint32_t real = page < g->rom_pages ? page : page % g->rom_pages;
    uint8_t *p = g->pages[real];
    if (!p) {
        uint32_t slot, n;
        if (g->used < g->nslots) {
            slot = g->used++;
        } else {                        /* round robin; slot 0 holds page 0, which is read constantly: never evict it */
            slot = g->clock + 1;
            if (slot >= g->nslots)
                slot = 1;
            g->clock = slot;
        }
        p = g->slots + slot * GBC_PAGE_SIZE;
        for (uint32_t i = 0; i < GBC_MAX_PAGES; i++)    /* drop the old page and any alias of it */
            if (g->pages[i] == p)
                g->pages[i] = 0;
        n = g->rd(g->user, real, p);
        if (n == 0) {
            g->err = GBC_ERR_ROMIO;
            g->gb.gb_frame = true;
        }
        for (; n < GBC_PAGE_SIZE; n++)
            p[n] = 0xff;
        g->pages[real] = p;
        g->faults++;
    }
    g->pages[page] = p;
    return p;
}
static inline const uint8_t *rom_page(gbc_t *g, uint32_t page)
{
    uint8_t *p;
    page &= GBC_MAX_PAGES - 1u;
    p = g->pages[page];
    return p ? p : page_fault(g, page);
}
static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
    return rom_page((gbc_t *)gb, addr >> 14)[addr & 0x3fffu];
}
static uint16_t rom_read16(struct gb_s *gb, const uint_fast32_t addr)
{
    uint32_t off = addr & 0x3fffu;
    const uint8_t *p = rom_page((gbc_t *)gb, addr >> 14);
    if (off <= 0x3ffeu)
        return (uint16_t)(p[off] | p[off + 1] << 8);
    return (uint16_t)(p[off] | rom_read(gb, addr + 1) << 8);
}
static uint32_t rom_read32(struct gb_s *gb, const uint_fast32_t addr)
{
    uint32_t off = addr & 0x3fffu;
    const uint8_t *p = rom_page((gbc_t *)gb, addr >> 14);
    if (off <= 0x3ffcu)
        return (uint32_t)p[off] | (uint32_t)p[off + 1] << 8 | (uint32_t)p[off + 2] << 16 | (uint32_t)p[off + 3] << 24;
    return (uint32_t)rom_read(gb, addr) | (uint32_t)rom_read(gb, addr + 1) << 8 |
           (uint32_t)rom_read(gb, addr + 2) << 16 | (uint32_t)rom_read(gb, addr + 3) << 24;
}

/* ---- cart RAM: addresses come from the game, so bound them ------------------------------------------------------ */
static uint8_t ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
    gbc_t *g = (gbc_t *)gb;
    return addr < g->ram_size ? g->ram[addr] : 0xff;
}
static void ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val)
{
    gbc_t *g = (gbc_t *)gb;
    if (addr < g->ram_size && g->ram[addr] != val) {
        g->ram[addr] = val;
        g->ram_writes++;
    }
}

/* The core carries on after a bad read or write; a bad opcode returns without spending cycles, so end the frame here
 * or gb_run_frame could spin on a ROM full of them. */
static void error_cb(struct gb_s *gb, const enum gb_error_e e, const uint16_t addr)
{
    gbc_t *g = (gbc_t *)gb;
    if (!g->err) {
        g->err = e == GB_INVALID_OPCODE ? GBC_ERR_OPCODE : e == GB_INVALID_READ ? GBC_ERR_READ : GBC_ERR_WRITE;
        g->err_addr = addr;
    }
    gb->gb_frame = true;
}

/* ---- 3:2 scaler: source lines 2k, 2k+1 become rows 3k, 3k+2, and row 3k+1 sits between them ---------------------- */
#define AVG565(a, b) ((uint16_t)((((uint32_t)(a) & 0xf7deu) + ((uint32_t)(b) & 0xf7deu)) >> 1))

static void draw_line(struct gb_s *gb, const uint8_t *px, const uint_fast8_t ly)
{
    gbc_t *g = (gbc_t *)gb;
    uint16_t *row, *d;
    uint16_t c[160];
    const int smooth = g->smooth;
    uint32_t odd;

    if (!g->fb || ly >= 144)
        return;
    if (gb->cgb.cgbMode) {
        const uint16_t *pal = gb->cgb.fixPalette;
        for (int x = 0; x < 160; x++)
            c[x] = pal[px[x] & 0x3fu];
    } else {
        for (int x = 0; x < 160; x++) {
            uint32_t v = px[x];
            c[x] = g->dmg_pal[((v >> 2) & 0x0cu) | (v & 3u)];
        }
    }
    odd = ly & 1u;
    row = g->fb + (uint32_t)(3u * (ly >> 1) + (odd ? 2u : 0u)) * g->pitch;
    d = row;
    for (int x = 0; x < 160; x += 2, d += 3) {
        uint16_t a = c[x], b = c[x + 1];
        d[0] = a;
        d[1] = smooth ? AVG565(a, b) : a;
        d[2] = b;
    }
    if (odd) {
        const uint16_t *up = row - 2u * g->pitch;
        uint16_t *mid = row - g->pitch;
        if (smooth) {
            for (int x = 0; x < GBC_SCREEN_W; x++)
                mid[x] = AVG565(up[x], row[x]);
        } else {
            for (int x = 0; x < GBC_SCREEN_W; x++)
                mid[x] = up[x];
        }
    }
}

/* ---- API --------------------------------------------------------------------------------------------------------- */
uint32_t gbc_ctx_size(void) { return (uint32_t)((sizeof(gbc_t) + 3u) & ~3u); }

int gbc_check_header(const uint8_t *r, uint32_t file_size, uint32_t *ram_size)
{
    /* these mirror the core's own cart tables (gb_init), which index them without checks */
    static const uint8_t mbc_ok[32] = { 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1,
                                        1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0 };
    static const uint8_t has_ram[32] = { 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0,
                                         1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0 };
    static const uint8_t ram_2k[6] = { 0, 1, 4, 16, 64, 32 };   /* in units of 2 KiB */
    const uint32_t type = r[0x147];
    uint8_t sum = 0;

    if (file_size < 0x150u || file_size > GBC_MAX_PAGES * GBC_PAGE_SIZE)
        return GBC_E_HEADER;
    for (uint32_t i = 0x134u; i <= 0x14cu; i++)
        sum = (uint8_t)(sum - r[i] - 1u);
    if (sum != r[0x14d])
        return GBC_E_CHECKSUM;
    if (type >= 32u || !mbc_ok[type] || r[0x148] > 8u)
        return GBC_E_CART;
    if (r[0x149] > 5u)
        return GBC_E_RAMSIZE;
    if (type == 5u || type == 6u)
        *ram_size = 0x200u;             /* MBC2: 512 half-bytes */
    else
        *ram_size = has_ram[type] ? (uint32_t)ram_2k[r[0x149]] * 2048u : 0u;
    return GBC_OK;
}

void gbc_setup(gbc_t *g, uint8_t *slots, uint32_t nslots, uint32_t rom_pages, gbc_read_page_fn rd, void *user,
               uint8_t *ram, uint32_t ram_size)
{
    /* colours for original Game Boy games, close to what a CGB picks for a game it has no palette for */
    static const uint16_t pal[12] = { 0xffffu, 0xfc10u, 0x91c7u, 0x0000u,      /* OBJ0 */
                                      0xffffu, 0xfc10u, 0x91c7u, 0x0000u,      /* OBJ1 */
                                      0xffffu, 0x651fu, 0x001fu, 0x0000u };    /* BG */
    volatile uint8_t *b = (volatile uint8_t *)g;
    for (uint32_t i = 0; i < sizeof *g; i++)
        b[i] = 0;
    g->slots = slots;
    g->nslots = nslots;
    g->rom_pages = rom_pages;
    g->rd = rd;
    g->user = user;
    g->ram = ram;
    g->ram_size = ram_size;
    for (int i = 0; i < 12; i++)
        g->dmg_pal[i] = pal[i];
}

uint8_t *gbc_prefill_slot(gbc_t *g, uint32_t page)
{
    uint8_t *p;
    if (g->used >= g->nslots || page >= g->rom_pages)
        return 0;
    p = g->slots + g->used * GBC_PAGE_SIZE;
    g->used++;
    g->pages[page] = p;
    return p;
}

int gbc_start(gbc_t *g)
{
    minigb_apu_audio_init(&g->apu);
    enum gb_init_error_e e = gb_init(&g->gb, rom_read, rom_read16, rom_read32, ram_read, ram_write, error_cb, g);
    if (e != GB_INIT_NO_ERROR)
        return e == GB_INIT_INVALID_CHECKSUM ? GBC_E_CHECKSUM : GBC_E_CART;
    gb_init_lcd(&g->gb, draw_line);
    return GBC_OK;
}

void gbc_set_rtc(gbc_t *g, uint32_t sec, uint32_t min, uint32_t hour, uint32_t days)
{
    struct tm t;
    t.tm_sec = (int)sec;
    t.tm_min = (int)min;
    t.tm_hour = (int)hour;
    t.tm_yday = (int)(days & 0x1ffu);
    gb_set_rtc(&g->gb, &t);
}

void gbc_set_fb(gbc_t *g, uint16_t *rows, uint32_t pitch) { g->fb = rows; g->pitch = pitch; }
void gbc_set_smooth(gbc_t *g, int on) { g->smooth = on; }
void gbc_set_buttons(gbc_t *g, uint32_t pressed) { g->gb.direct.joypad = (uint8_t)~pressed; }

void gbc_run_frame(gbc_t *g, int draw)
{
    if (g->err)
        return;
    g->gb.display.lcd_draw_line = draw ? draw_line : 0;
    gb_run_frame_dualfetch(&g->gb);
}

void gbc_reset(gbc_t *g) { minigb_apu_audio_init(&g->apu); gb_reset(&g->gb); g->err = 0; }

int gbc_has_rtc(const gbc_t *g)
{
    const uint8_t *p0 = g->pages[0];
    return p0 && (p0[0x147] == 0x0f || p0[0x147] == 0x10);
}

void gbc_get_rtc(const gbc_t *g, uint8_t regs[5])
{
    memcpy(regs, g->gb.rtc_real.bytes, 5);
}

void gbc_put_rtc(gbc_t *g, const uint8_t regs[5], uint32_t elapsed)
{
    union cart_rtc r;
    memcpy(r.bytes, regs, 5);
    if (!(r.reg.high & 0x40u)) {       /* the game can halt the clock */
        uint32_t s = r.reg.sec + elapsed;
        uint32_t m = r.reg.min + s / 60u;
        uint32_t h = r.reg.hour + m / 60u;
        uint32_t d = (((uint32_t)r.reg.high & 1u) << 8 | r.reg.yday) + h / 24u;
        r.reg.sec = (uint8_t)(s % 60u);
        r.reg.min = (uint8_t)(m % 60u);
        r.reg.hour = (uint8_t)(h % 24u);
        if (d > 511u)
            r.reg.high |= 0x80u;       /* day counter carry, as the chip sets it */
        r.reg.yday = (uint8_t)d;
        r.reg.high = (uint8_t)((r.reg.high & 0xfeu) | ((d >> 8) & 1u));
    }
    memcpy(g->gb.rtc_real.bytes, r.bytes, 5);
    memcpy(g->gb.rtc_latched.bytes, r.bytes, 5);
}

void gbc_audio_frame(gbc_t *g, int16_t *mono)
{
    uint32_t i;
    minigb_apu_audio_callback(&g->apu, g->apu_out);
    for (i = 0; i < GBC_AUDIO_SAMPLES; i++)
        mono[i] = (int16_t)((g->apu_out[i * 2u] + g->apu_out[i * 2u + 1u]) / 2);
}

int gbc_error(const gbc_t *g) { return g->err; }
uint32_t gbc_error_addr(const gbc_t *g) { return g->err_addr; }
uint32_t gbc_ram_writes(const gbc_t *g) { return g->ram_writes; }
uint32_t gbc_page_faults(const gbc_t *g) { return g->faults; }
int gbc_is_cgb(const gbc_t *g) { return g->gb.cgb.cgbMode; }

#ifdef GBC_HOST
static void (*host_tx)(uint8_t);
static void serial_tx(struct gb_s *gb, const uint8_t b) { (void)gb; if (host_tx) host_tx(b); }
static enum gb_serial_rx_ret_e serial_rx(struct gb_s *gb, uint8_t *b) { (void)gb; *b = 0xff; return GB_SERIAL_RX_NO_CONNECTION; }
void gbc_host_serial(gbc_t *g, void (*tx)(uint8_t)) { host_tx = tx; gb_init_serial(&g->gb, serial_tx, serial_rx); }
#endif
