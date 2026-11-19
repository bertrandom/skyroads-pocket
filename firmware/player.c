/* The music player: the game's songs one at a time, with the game's own
 * player (../../port/music.c) and the Pocket's OPL2, to listen to them
 * apart from the game.  A Core Setting ("Music player", SETTINGS bit 0)
 * turns it on: pocket.c keeps the game aside (an excursion, savestate.c)
 * and calls player_run() inside the timer interrupt, with interrupts off;
 * it returns when the setting is turned off, and the game carries on.
 *
 *   Up / Down     choose a song
 *   A             play it; play / pause the one playing
 *   B             stop
 *   Left / Right  10 seconds back / forward (held: repeated)
 *   L / R         60 seconds back / forward
 *
 * A song is a list of events (music.c) from its start: delays, notes,
 * instruments, volumes, a loop mark and the jump back to it, which ends
 * the song (it then plays on from the mark).  Its length is the sum of the
 * delays before the jump, in ticks of the timer (the PIT rate the game
 * sets, ~180 Hz).  Seeking plays the song from its start to the place with
 * the chip not told (fast: no sound), then tells the chip every register
 * as the player left it. */
#include "compat.h"
#include "protos.h"
#include "pocket_hw.h"
#include "font6x11.h"

#define DS16(off) (*(volatile uint16_t *)(g_ds + (uint16_t)(off)))
#define DS8(off)  (*(volatile uint8_t  *)(g_ds + (uint16_t)(off)))

void music_tick(void);                  /* music.c */
void ss_refuse(void);                   /* savestate.c */
void opl_raw(uint8_t reg, uint8_t val); /* pocket.c */
uint8_t opl_reg(uint8_t reg);
void opl_replay(void);
extern int opl_mute;

#define TICK_HZ   180                   /* 1193182 / 6628 */
#define MAX_SONGS 32

/* colours (palette entries the player sets) */
enum { C_BG, C_TEXT, C_DIM, C_HI, C_SELBG, C_BAR, C_BARBG, C_PLAY };

static uint8_t *s_vga;
static int      s_nsongs, s_sel, s_cur = -1, s_playing, s_paused;
static uint32_t s_pos, s_len, s_loop;   /* ticks */
static uint16_t s_start, s_prev_ptr;

/* ------------------------------------------------------------------ */
/* drawing: mode 13h, 320x200, one byte a pixel                         */
/* ------------------------------------------------------------------ */
static void rect(int x, int y, int w, int h, uint8_t c)
{
    int i;
    for (; h > 0; h--, y++)
        for (i = 0; i < w; i++) s_vga[y * 320 + x + i] = c;
}

static void text(int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    for (; *s; s++, x += FONT_W) {
        int c = (unsigned char)*s, r, b;
        if (c < 32 || c > 126) c = '?';
        for (r = 0; r < FONT_H; r++) {
            uint8_t bits = font6x11[c - 32][r];
            for (b = 0; b < FONT_W; b++)
                s_vga[(y + r) * 320 + x + b] = (bits & (0x20 >> b)) ? fg : bg;
        }
    }
}

static void mmss(char *out, uint32_t ticks)
{
    uint32_t s = ticks / TICK_HZ;
    out[0] = (char)('0' + s / 600 % 10); out[1] = (char)('0' + s / 60 % 10); out[2] = ':';
    out[3] = (char)('0' + s % 60 / 10); out[4] = (char)('0' + s % 10); out[5] = 0;
}

static void song_name(char *out, int n)
{
    static const char *const named[] = { "Title", "Menu" };
    char *p = out;
    *p++ = (char)('0' + n / 10); *p++ = (char)('0' + n % 10); *p++ = ' ';
    if (n < 2) { const char *q = named[n]; while (*q) *p++ = *q++; }
    else {
        const char *q = "Road song ";
        int r = n - 1;
        while (*q) *p++ = *q++;
        if (r >= 10) *p++ = (char)('0' + r / 10);
        *p++ = (char)('0' + r % 10);
    }
    while (p < out + 16) *p++ = ' ';
    *p = 0;
}

static void draw_list(void)
{
    int i;
    char name[20];
    for (i = 0; i < s_nsongs; i++) {
        int col = i < (s_nsongs + 1) / 2 ? 0 : 1, row = col ? i - (s_nsongs + 1) / 2 : i;
        int x = 14 + col * 156, y = 22 + row * 12;
        uint8_t bg = i == s_sel ? C_SELBG : C_BG;
        song_name(name, i);
        text(x, y, i == s_cur ? ">" : " ", C_PLAY, bg);
        text(x + FONT_W, y, name, i == s_sel ? C_HI : C_TEXT, bg);
    }
}

static void draw_status(void)
{
    char line[54], t[6];
    int w;
    if (s_cur < 0) {
        text(14, 124, "Nothing playing                        ", C_DIM, C_BG);
        text(14, 138, "                          ", C_TEXT, C_BG);
        rect(14, 154, 292, 8, C_BARBG);
        return;
    }
    song_name(line, s_cur);
    text(14, 124, line, C_TEXT, C_BG);
    text(14 + 17 * FONT_W, 124, s_paused ? "PAUSED   " : s_playing ? "PLAYING  " : "STOPPED  ", C_HI, C_BG);
    mmss(t, s_pos);
    memcpy(line, t, 5); line[5] = ' '; line[6] = '/'; line[7] = ' ';
    mmss(t, s_len);
    memcpy(line + 8, t, 6);
    text(14, 138, line, C_TEXT, C_BG);
    if (s_loop) {
        mmss(t, s_loop);
        text(14 + 15 * FONT_W, 138, "(loops to ", C_DIM, C_BG);
        text(14 + 25 * FONT_W, 138, t, C_DIM, C_BG);
        text(14 + 30 * FONT_W, 138, ")", C_DIM, C_BG);
    } else text(14 + 15 * FONT_W, 138, "                ", C_DIM, C_BG);
    w = s_len ? (int)((uint64_t)s_pos * 292 / s_len) : 0;
    if (w > 292) w = 292;
    rect(14, 154, w, 8, C_BAR);
    rect(14 + w, 154, 292 - w, 8, C_BARBG);
}

static void draw_all(void)
{
    static const uint32_t pal[] = { 0x000018, 0xe0e0e0, 0x707890, 0xffd040, 0x283070, 0x40c0ff, 0x202848, 0x60ff60 };
    unsigned i;
    for (i = 0; i < sizeof pal / sizeof pal[0]; i++) hw_wr(R_PALETTE + i * 4, pal[i]);
    rect(0, 0, 320, 200, C_BG);
    text(14, 4, "SKYROADS MUSIC PLAYER", C_HI, C_BG);
    draw_list();
    draw_status();
    text(14, 170, "Up/Down song  A play/pause  B stop", C_DIM, C_BG);
    text(14, 182, "Left/Right 10 s  L/R 60 s", C_DIM, C_BG);
}

/* ------------------------------------------------------------------ */
/* songs                                                               */
/* ------------------------------------------------------------------ */
/* muzax.lzs starts with a table of 6-byte entries (offset, start, size),
 * ending where song 0 starts; the last entries are empty (size 0) */
static int count_songs(void)
{
    SkyFile *f = sky_open("muzax.lzs");
    uint8_t e[6];
    int n = 0, slots = MAX_SONGS;
    if (!f) return 0;
    while (n < slots && sky_read(f, e, 6) == 6) {
        if (n == 0) slots = (e[0] | e[1] << 8) / 6;
        if ((e[4] | e[5] << 8) == 0) break;
        n++;
    }
    sky_close(f);
    return n > MAX_SONGS ? MAX_SONGS : n;
}

/* the song's length and loop mark: its events up to the jump back */
static void measure(void)
{
    uint16_t si = s_start;
    uint32_t t = 0, k;
    s_loop = 0;
    for (k = 0; k < 200000; k++) {
        uint16_t w = DS16(si);
        si = (uint16_t)(si + 2);
        if ((w & 7) == 0) t += w >> 8;
        else if ((w & 7) == 6) s_loop = t;
        else if ((w & 7) == 5) break;
    }
    s_len = t ? t : 1;
}

/* the player at the song's start, the chip silent */
static void rewind(void)
{
    FUN_1000_5889();                    /* keys off, levels down */
    DS16(0x31a2) = s_start;
    DS16(0x31a4) = s_start;
    DS8(0xc8f) = 0;
    DS8(0x31b2) = 0;
    s_prev_ptr = s_start;
    s_pos = 0;
}

static void start(int n)
{
    DS16(0x41b6) = 0;                   /* no file error left over */
    DS16(0x4528) = 0;                   /* sound on */
    DS16(0xbc2) = 0xffff;               /* load it even if it is the one loaded */
    FUN_1000_57a8(n);
    DS16(0x41b6) = 0;
    if (DS16(0xbc2) != n) { s_cur = -1; s_playing = 0; return; }
    s_cur = n;
    s_start = DS16(0x31a2);
    measure();
    rewind();
    s_playing = 1;
    s_paused = 0;
}

static void stop(void)
{
    if (s_cur < 0) return;
    rewind();
    s_playing = 0;
    s_paused = 0;
}

static void pause(int on)
{
    unsigned ch;
    s_paused = on;
    if (on) {                           /* keys off on the chip, not in the player's view */
        for (ch = 0; ch < 9; ch++) opl_raw((uint8_t)(0xb0 + ch), (uint8_t)(opl_reg((uint8_t)(0xb0 + ch)) & ~0x20));
        opl_raw(0xbd, (uint8_t)(opl_reg(0xbd) & 0xe0));
    } else opl_replay();
}

static void seek(int32_t ticks)
{
    uint32_t to, i;
    if (s_cur < 0) return;
    to = ticks < 0 ? 0 : (uint32_t)ticks;
    if (to >= s_len) to = s_len - 1;
    rewind();
    opl_mute = 1;
    for (i = 0; i < to; i++) music_tick();
    opl_mute = 0;
    s_prev_ptr = DS16(0x31a2);
    s_pos = to;
    if (!s_paused) opl_replay();
    if (!s_playing) { s_playing = 1; s_paused = 1; }   /* stopped: seeking pauses there */
}

/* ------------------------------------------------------------------ */
/* the loop: one pass per timer tick                                    */
/* ------------------------------------------------------------------ */
static int tick_pending(void)
{
    uint32_t mip;
    __asm__ volatile ("csrr %0, mip" : "=r"(mip));
    return (mip & 0x80) != 0;
}

void player_run(void)
{
    uint32_t last = 0, held = 0, n = 0;
    s_vga = g_mem + VGA_BASE;
    s_sel = 0; s_cur = -1; s_playing = 0; s_paused = 0;
    FUN_1000_58b1();                    /* the chip and the percussion, as the game starts */
    s_nsongs = count_songs();
    draw_all();
    for (;;) {
        uint32_t keys, press, rep;
        while (!tick_pending()) { }
        hw_wr(R_TIMER_ACK, 1);
        if (!(hw_rd(R_SETTINGS) & 1)) break;
        ss_refuse();                    /* no sleep while the player runs */

        keys = hw_rd(R_KEYS);
        press = keys & ~last;
        rep = 0;                        /* held arrows repeat: 0.4 s, then 8 a second */
        if (keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT) && keys == last) {
            if (++held >= 72 && (held - 72) % 22 == 0) rep = keys;
        } else held = 0;
        last = keys;
        press |= rep;

        if (press & (KEY_UP | KEY_DOWN)) {
            if ((press & KEY_UP) && s_sel > 0) s_sel--;
            if ((press & KEY_DOWN) && s_sel < s_nsongs - 1) s_sel++;
            draw_list();
        }
        if (press & KEY_A) {
            if (s_sel != s_cur || !s_playing) start(s_sel);
            else pause(!s_paused);
            draw_list();
            draw_status();
        }
        if (press & KEY_B) { stop(); draw_status(); }
        if (press & (KEY_LEFT | KEY_RIGHT | KEY_L1 | KEY_R1)) {
            int32_t d = (press & KEY_LEFT) ? -10 : (press & KEY_RIGHT) ? 10 : (press & KEY_L1) ? -60 : 60;
            seek((int32_t)s_pos + d * TICK_HZ);
            draw_status();
        }

        if (s_playing && !s_paused) {
            music_tick();
            s_pos++;
            if (DS16(0x31a2) < s_prev_ptr) s_pos = s_loop;  /* jumped back to the loop mark */
            s_prev_ptr = DS16(0x31a2);
            if (++n % 9 == 0) draw_status();               /* 20 times a second */
        }
    }
    FUN_1000_5889();                    /* silent: the game's music comes back with it */
}
