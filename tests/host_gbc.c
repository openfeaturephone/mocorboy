/* host_gbc.c - runs a ROM through gbc_core on the PC. Built and driven by check_gbc.py.
 *   host_gbc <rom> <frames> <out.ppm> <slots> [BUTTON@frame ...]
 * If GBC_PCM names a file, the sound goes there as raw mono 16-bit samples at GBC_AUDIO_RATE.
 * Writes the 240x216 frame (nearest-neighbour scaling, so check_gbc.py can recover the 160x144 picture exactly) and
 * prints serial output, faults and errors. A small <slots> exercises eviction. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gbc_core.h"

static FILE *rom;
static uint32_t rd(void *user, uint32_t page, uint8_t *dst)
{
    (void)user;
    fseek(rom, (long)(page * GBC_PAGE_SIZE), SEEK_SET);
    return (uint32_t)fread(dst, 1, GBC_PAGE_SIZE, rom);
}
static void tx(uint8_t b) { putchar(b); }

int main(int argc, char **argv)
{
    static uint16_t fb[GBC_SCREEN_H][GBC_SCREEN_W];
    uint8_t hdr[0x150];
    long size;
    uint32_t ram_size = 0, pages, slots;
    int frames, e;
    uint8_t *ram, *arena, *p0;
    gbc_t *g;
    FILE *f, *pcm;

    if (argc == 2 && !strcmp(argv[1], "--rtc")) {   /* the clock: 511 days 23:59:50 + 15 s wraps to day 0 with the carry */
        static const uint8_t in[5] = { 50, 59, 23, 255, 1 };
        static const uint8_t halted[5] = { 1, 2, 3, 4, 0x40 };
        uint8_t a[5], b[5];
        g = calloc(1, gbc_ctx_size());
        gbc_put_rtc(g, in, 15);
        gbc_get_rtc(g, a);
        gbc_put_rtc(g, halted, 1000);
        gbc_get_rtc(g, b);
        printf("rtc %d %d %d %d %d halted %d %d %d %d %d\n", a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
        return 0;
    }
    if (argc < 5)
        return 2;
    rom = fopen(argv[1], "rb");
    if (!rom)
        return 2;
    frames = atoi(argv[2]);
    slots = (uint32_t)atoi(argv[4]);
    fseek(rom, 0, SEEK_END);
    size = ftell(rom);
    fseek(rom, 0, SEEK_SET);
    if (fread(hdr, 1, sizeof hdr, rom) != sizeof hdr)
        return 2;
    e = gbc_check_header(hdr, (uint32_t)size, &ram_size);
    if (e) {
        printf("header error %d\n", e);
        return 3;
    }
    pages = (uint32_t)(size + GBC_PAGE_SIZE - 1) / GBC_PAGE_SIZE;
    ram = malloc(ram_size + 1);
    memset(ram, 0xff, ram_size + 1);
    arena = malloc(slots * GBC_PAGE_SIZE);
    g = malloc(gbc_ctx_size());
    gbc_setup(g, arena, slots, pages, rd, 0, ram, ram_size);
    p0 = gbc_prefill_slot(g, 0);
    rd(0, 0, p0);
    e = gbc_start(g);
    if (e) {
        printf("start error %d\n", e);
        return 3;
    }
    gbc_host_serial(g, tx);
    gbc_set_fb(g, &fb[0][0], GBC_SCREEN_W);
    {
        const char *pcm_name = getenv("GBC_PCM");
        pcm = pcm_name ? fopen(pcm_name, "wb") : 0;
    }
    for (int i = 0; i < frames; i++) {
        uint32_t b = 0;
        for (int a = 5; a < argc; a++) {        /* A@120 = press button A for 10 frames from frame 120 */
            int at = atoi(strchr(argv[a], '@') + 1);
            if (i >= at && i < at + 10) {
                switch (argv[a][0]) {
                case 'A': b |= GBC_A; break;
                case 'B': b |= GBC_B; break;
                case 'S': b |= GBC_START; break;
                case 'E': b |= GBC_SELECT; break;
                case 'U': b |= GBC_UP; break;
                case 'D': b |= GBC_DOWN; break;
                case 'L': b |= GBC_LEFT; break;
                case 'R': b |= GBC_RIGHT; break;
                default: break;
                }
            }
        }
        gbc_set_buttons(g, b);
        gbc_run_frame(g, 1);
        if (pcm) {
            static int16_t samples[GBC_AUDIO_SAMPLES];
            gbc_audio_frame(g, samples);
            fwrite(samples, 2, GBC_AUDIO_SAMPLES, pcm);
        }
        if (gbc_error(g)) {
            printf("\nemulation error %d at %04x, frame %d\n", gbc_error(g), (unsigned)gbc_error_addr(g), i);
            break;
        }
    }
    if (pcm)
        fclose(pcm);
    f = fopen(argv[3], "wb");
    fprintf(f, "P6\n%d %d\n255\n", GBC_SCREEN_W, GBC_SCREEN_H);
    for (int y = 0; y < GBC_SCREEN_H; y++)
        for (int x = 0; x < GBC_SCREEN_W; x++) {
            uint16_t c = fb[y][x];
            uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("\ncgb=%d faults=%u ram_writes=%u err=%d\n", gbc_is_cgb(g), (unsigned)gbc_page_faults(g),
           (unsigned)gbc_ram_writes(g), gbc_error(g));
    return 0;
}
