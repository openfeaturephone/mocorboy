/* gbc_core.h - the Game Boy / Game Boy Color machine for gbc.c, free of firmware calls so it also builds on the PC.
 * Wraps Walnut-CGB (gbc/vendor/walnut_cgb): ROM paging, cart RAM, and the 3:2 scaler to 240x216 RGB565. */
#ifndef GBC_CORE_H
#define GBC_CORE_H
#include <stdint.h>

#define GBC_PAGE_SIZE   0x4000u
#define GBC_MAX_PAGES   512u          /* MBC5: 8 MB */
#define GBC_SCREEN_W    240
#define GBC_SCREEN_H    216
#define GBC_FRAME_US    16742u        /* 70224 clocks at 4194304 Hz */
#define GBC_AUDIO_RATE  32768u
#define GBC_AUDIO_SAMPLES 548u        /* mono samples per frame */

/* Game Boy buttons, as the core's joypad bits */
#define GBC_A      0x01u
#define GBC_B      0x02u
#define GBC_SELECT 0x04u
#define GBC_START  0x08u
#define GBC_RIGHT  0x10u
#define GBC_LEFT   0x20u
#define GBC_UP     0x40u
#define GBC_DOWN   0x80u

enum { GBC_OK = 0, GBC_E_HEADER, GBC_E_CART, GBC_E_CHECKSUM, GBC_E_RAMSIZE };
enum { GBC_ERR_NONE = 0, GBC_ERR_OPCODE, GBC_ERR_READ, GBC_ERR_WRITE, GBC_ERR_ROMIO };

typedef struct gbc gbc_t;

/* Reads one ROM page from wherever it lives: returns the bytes read (a short last page is padded with 0xFF), 0 on failure */
typedef uint32_t (*gbc_read_page_fn)(void *user, uint32_t page, uint8_t *dst);

uint32_t gbc_ctx_size(void);          /* bytes to allocate for gbc_t; the block must be 4-aligned */
/* A ROM header can be checked from the first page alone. ram_size gets the cart RAM bytes the game needs. */
int gbc_check_header(const uint8_t *page0, uint32_t file_size, uint32_t *ram_size);
/* slots: arena of nslots * GBC_PAGE_SIZE bytes, nslots >= 4. ram: ram_size bytes, already holding the save or 0xFF */
void gbc_setup(gbc_t *g, uint8_t *slots, uint32_t nslots, uint32_t rom_pages, gbc_read_page_fn rd, void *user,
               uint8_t *ram, uint32_t ram_size);
/* Hand page data to the cache ahead of time (the loading screen): page 0 first. Returns the slot for the caller to
 * fill, or 0 when the cache is full. */
uint8_t *gbc_prefill_slot(gbc_t *g, uint32_t page);
int gbc_start(gbc_t *g);              /* after page 0 is resident: GBC_OK or GBC_E_* */
void gbc_set_rtc(gbc_t *g, uint32_t sec, uint32_t min, uint32_t hour, uint32_t days);
/* What a battery keeps on an MBC3 clock cart: five registers. gbc_put_rtc adds the seconds that passed since they were saved. */
int gbc_has_rtc(const gbc_t *g);
void gbc_get_rtc(const gbc_t *g, uint8_t regs[5]);
void gbc_put_rtc(gbc_t *g, const uint8_t regs[5], uint32_t elapsed);

void gbc_set_fb(gbc_t *g, uint16_t *rows, uint32_t pitch);   /* top-left of the 240x216 area; NULL = do not draw */
void gbc_set_smooth(gbc_t *g, int on);
void gbc_set_buttons(gbc_t *g, uint32_t pressed);            /* GBC_* mask */
void gbc_run_frame(gbc_t *g, int draw);
void gbc_reset(gbc_t *g);
/* After each gbc_run_frame: GBC_AUDIO_SAMPLES of mono output for that frame. Skipping it only skips the sound. */
void gbc_audio_frame(gbc_t *g, int16_t *mono);

int gbc_error(const gbc_t *g);        /* GBC_ERR_*, sticky until gbc_reset */
uint32_t gbc_error_addr(const gbc_t *g);
uint32_t gbc_ram_writes(const gbc_t *g);    /* counts cart RAM writes that changed a byte */
uint32_t gbc_page_faults(const gbc_t *g);
int gbc_is_cgb(const gbc_t *g);

#ifdef GBC_HOST
void gbc_host_serial(gbc_t *g, void (*tx)(uint8_t));
#endif
#endif
