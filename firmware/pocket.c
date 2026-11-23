/* SkyRoads on the Analogue Pocket: the platform layer of the port.
 *
 * Replaces the SDL platform layer, ../decomp/port/sdl (main.c, platform.c,
 * hw.c and the mixer in audio.c).  The game code itself (runtime.c,
 * manual.c, render.c, video.c, music.c and the generated game.c) is shared
 * unchanged.
 *
 * There are no threads: the game runs on the CPU, and the SoC's timer
 * interrupt (~180 Hz, the PIT rate the game programs) does what the DOS
 * interrupts did: the game's INT 8 handler (music, PC speaker effects),
 * the keyboard (the controller's buttons become key presses, through the
 * game's INT 9 handler while it is installed, otherwise the BIOS keyboard
 * buffer) and feeding the Sound Blaster samples to the audio hardware.
 *
 * The screen is the VGA memory itself: the SoC's video RAM is mapped at
 * g_mem + 0xA0000, so there is nothing to copy; the palette goes to the
 * SoC's colour table as the game sets it. */
#include "compat.h"
#include "protos.h"
#include "image_data.h"
#include "pocket_hw.h"

#define DS16(off) (*(volatile uint16_t *)(g_ds + (off)))
#define DS8(off)  (*(volatile uint8_t  *)(g_ds + (off)))

void entry(void);
void music_tick(void);                  /* music.c */

static uint32_t s_sys_hz;

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */
static uint64_t raw_cycles(void)
{
    uint32_t lo = hw_rd(R_CYCLES_LO);   /* latches HI */
    return ((uint64_t)hw_rd(R_CYCLES_HI) << 32) | lo;
}

/* the counter starts at 0 when the core is loaded, so waking from sleep
 * would put it back in time: a wait in progress (SDL_Delay, the menus'
 * key wait) would spin until it caught up.  The game's time is the
 * counter plus an offset that a save state carries on from (and that
 * stops while the game is kept aside: sleep, the music player). */
static volatile uint64_t s_cyc_off;  /* volatile: a wait reads it again after a wake */
static uint64_t s_cyc_saved;
static uint64_t cycles(void) { return raw_cycles() + s_cyc_off; }

uint64_t plat_millis(void) { return cycles() / (s_sys_hz / 1000u); }

#ifdef POCKET_SIM
static volatile uint64_t s_idle;        /* cycles spent waiting (load report) */
#endif

void SDL_Delay(uint32_t ms)
{
    uint64_t start = cycles(), end = start + (uint64_t)ms * (s_sys_hz / 1000u);
    while (cycles() < end) { }
#ifdef POCKET_SIM
    s_idle += cycles() - start;
#endif
}

/* ------------------------------------------------------------------ */
/* the timer interrupt: INT 8 (1000:3afb), see hw.c                    */
/* ------------------------------------------------------------------ */
static volatile int s_timer_isr_on, s_kbd_isr_on;
static volatile uint32_t s_bios_ticks;

void speaker_set_divisor(uint16_t div);

static void timer_isr(void)
{
    uint8_t c;
    music_tick();
    c = DS8(0x319e);
    if (c == 0 || c == 5) {
        uint16_t bx, cx;
        DS16(0x160c) = (uint16_t)(DS16(0x160c) + 1);
        bx = DS16(0xba0);
        cx = *(volatile uint16_t *)(g_ds + bx);
        if (cx != 0) DS16(0xba0) = (uint16_t)(bx + 2);
        speaker_set_divisor((uint16_t)(cx + 2));
    }
    c--;
    if ((int8_t)c < 0) {
        c = 9;
        s_bios_ticks++;                 /* chained BIOS tick, 18.2 Hz */
    }
    DS8(0x319e) = c;
}

uint32_t timer_ticks(void) { return s_bios_ticks; }
void hw_timer_isr_install(int on) { s_timer_isr_on = on; }

/* ------------------------------------------------------------------ */
/* keyboard: INT 9 (1000:3bb0) and the BIOS buffer                     */
/* ------------------------------------------------------------------ */
static void kbd_isr(uint8_t al)
{
    uint16_t bx = 0xba2;
    while (al != DS8(bx)) {
        bx++;
        if ((DS8(bx) & 0x7f) == 0) break;
    }
    DS8(bx) = (uint8_t)((DS8(bx) & 0x7f) | ((al & 0x80) ^ 0x80));
}

void hw_kbd_isr_install(int on) { s_kbd_isr_on = on; }

#define KEYQ 32
static volatile uint16_t s_keyq[KEYQ];
static volatile int s_key_r, s_key_w;

static void bios_key_push(uint16_t code)
{
    int n = (s_key_w + 1) % KEYQ;
    if (n == s_key_r) return;
    s_keyq[s_key_w] = code;
    s_key_w = n;
}

int kb_read_key(void)
{
    uint16_t c;
    if (s_key_r == s_key_w) return -1;
    c = s_keyq[s_key_r];
    s_key_r = (s_key_r + 1) % KEYQ;
    return c;
}

int kb_key_available(void) { return s_key_r != s_key_w; }
void kb_clear(void) { s_key_r = s_key_w; }

static void key_event(uint8_t sc, uint16_t bios, int down)
{
    if (s_kbd_isr_on) { kbd_isr((uint8_t)(sc | (down ? 0 : 0x80))); return; }
    if (down && bios) bios_key_push(bios);
}

/* the controller as the keyboard.  During a run the game's INT 9 handler
 * is installed and takes scan codes; elsewhere (the menus, the world map)
 * it reads the BIOS buffer.  So the face buttons A, B and Y (right, bottom,
 * left) are Space, jump, in a run and Enter in the menus; X (top) is Esc;
 * Start Enter, Select Esc.  Only the d-pad repeats when held: a held jump
 * button must not go through several screens. */
static const struct { uint32_t bit; uint8_t sc; uint16_t bios; uint8_t repeat; } s_map[] = {
    { KEY_UP,     0x48, 0x4800, 1 }, { KEY_DOWN,  0x50, 0x5000, 1 },
    { KEY_LEFT,   0x4b, 0x4b00, 1 }, { KEY_RIGHT, 0x4d, 0x4d00, 1 },
    { KEY_A,      0x39, 0x1c0d, 0 }, { KEY_B,     0x39, 0x1c0d, 0 },
    { KEY_Y,      0x39, 0x1c0d, 0 }, { KEY_X,     0x01, 0x011b, 0 },
    { KEY_START,  0x1c, 0x1c0d, 0 }, { KEY_SELECT,0x01, 0x011b, 0 },
};
#define NMAP (sizeof s_map / sizeof s_map[0])

/* BIOS typematic repeat for held buttons (in menus): 0.5 s, then 10 a second */
#define REPEAT_DELAY 90
#define REPEAT_EVERY 18

static void poll_buttons(void)
{
    static uint32_t last;
    static uint16_t held[NMAP];
    uint32_t now = hw_rd(R_KEYS), changed = now ^ last;
    unsigned i;
    for (i = 0; i < NMAP; i++) {
        int down = (now & s_map[i].bit) != 0;
        if (changed & s_map[i].bit) {
            key_event(s_map[i].sc, s_map[i].bios, down);
            held[i] = 0;
        } else if (down && s_map[i].repeat && !s_kbd_isr_on && ++held[i] >= REPEAT_DELAY) {
            bios_key_push(s_map[i].bios);
            held[i] = REPEAT_DELAY - REPEAT_EVERY;
        }
    }
    last = now;
}

#ifndef POCKET_HOST
/* The firmware is linked with --wrap=FUN_1000_5fad (the game's "read a
 * key"), and link.ld puts two of the game's menus between symbols, so the
 * return address says which menu is reading:
 *
 * - the main menu (1000:4e36): Esc quits to DOS (1000:0046), which on the
 *   Pocket leaves a still picture, so Esc is dropped there;
 * - the controls menu (1000:4c04): Keyboard, Joystick, Mouse, then sound
 *   on and off (0 to 4), a cursor moved by the arrows; there is neither a
 *   joystick nor a mouse, so arrows that would move onto them are dropped,
 *   or, Up from "sound off" (onto Joystick), sent as Left then Up (onto
 *   Keyboard).  The cursor is followed here by the menu's own rules, from
 *   0 each time the menu starts reading.
 *
 * Esc everywhere else is untouched. */
extern char __menu_start[], __menu_end[], __ctlmenu_start[], __ctlmenu_end[];
uint16_t __real_FUN_1000_5fad(void);

static int s_ctl_live, s_ctl_sel;
static uint16_t s_ctl_next;

static void ctl_follow(uint16_t k)       /* 1000:4c04's cursor */
{
    if (k == 0x4800) { if (s_ctl_sel == 3) s_ctl_sel = 0; if (s_ctl_sel == 4) s_ctl_sel = 1; }
    else if (k == 0x4b00) { if (s_ctl_sel != 0) s_ctl_sel--; }
    else if (k == 0x4d00) { if (s_ctl_sel < 4) s_ctl_sel++; }
    else if (k == 0x5000) { if (s_ctl_sel == 0) s_ctl_sel = 3; else if (s_ctl_sel < 3) s_ctl_sel = 4; }
}

static uint16_t ctl_key(void)
{
    uint16_t k;
    if (!s_ctl_live) { s_ctl_live = 1; s_ctl_sel = 0; s_ctl_next = 0; }
    if (s_ctl_next) { k = s_ctl_next; s_ctl_next = 0; ctl_follow(k); return k; }
    for (;;) {
        k = __real_FUN_1000_5fad();
        if (k == 0x4b00 && s_ctl_sel == 3) continue;            /* onto Mouse */
        if (k == 0x4d00 && s_ctl_sel == 0) continue;            /* onto Joystick */
        if (k == 0x4800 && s_ctl_sel == 4) { k = 0x4b00; s_ctl_next = 0x4800; }
        ctl_follow(k);
        return k;
    }
}

uint16_t __wrap_FUN_1000_5fad(void)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    int in_menu = ra >= (uintptr_t)__menu_start && ra < (uintptr_t)__menu_end;
    if (ra >= (uintptr_t)__ctlmenu_start && ra < (uintptr_t)__ctlmenu_end) return ctl_key();
    s_ctl_live = 0;
    for (;;) {
        uint16_t k = __real_FUN_1000_5fad();
        if (!(in_menu && k == 0x1b)) return k;
    }
}
#endif

/* ------------------------------------------------------------------ */
/* sound: the OPL2 and the sample queue are in the SoC                 */
/* ------------------------------------------------------------------ */
static const uint8_t *volatile s_sb_data;
static volatile uint32_t s_sb_left;

static uint8_t s_opl_regs[256];         /* what the OPL2 was told (save states) */
int opl_mute;                           /* music player: seeking, the chip not told */

void opl_raw(uint8_t reg, uint8_t val)
{
    while (hw_rd(R_OPL_FREE) == 0) { }
    hw_wr(R_OPL, (uint32_t)reg << 8 | val);
}

void opl_out(uint8_t reg, uint8_t val)
{
    s_opl_regs[reg] = val;
    if (!opl_mute) opl_raw(reg, val);
}

uint8_t opl_reg(uint8_t reg) { return s_opl_regs[reg]; }

/* tell the chip everything it was told: the timers aside, and the key-on
 * registers (and rhythm) last */
void opl_replay(void)
{
    unsigned i;
    for (i = 1; i < 256; i++)
        if (i != 2 && i != 3 && i != 4 && i != 0xbd && !(i >= 0xb0 && i <= 0xb8)) opl_raw((uint8_t)i, s_opl_regs[i]);
    for (i = 0xb0; i <= 0xb8; i++) opl_raw((uint8_t)i, s_opl_regs[i]);
    opl_raw(0xbd, s_opl_regs[0xbd]);
}
void opl_tick(void) {}

static void sb_feed(void)
{
    uint32_t n = hw_rd(R_PCM_FREE);
    while (n-- && s_sb_left) {
        hw_wr(R_PCM, *s_sb_data++);
        s_sb_left--;
    }
}

static int irq_save(void);
static void irq_restore(int on);
void plat_halt(int code);               /* crt0.S / host_hal.c */

void sb_play(const uint8_t *data, uint16_t len, uint8_t time_const)
{
    int irq = irq_save();
    uint32_t rate = 1000000u / (256u - time_const);
    hw_wr(R_PCM_FLUSH, 1);
    hw_wr(R_PCM_PERIOD, s_sys_hz / rate);
    s_sb_data = data;
    s_sb_left = len;
    sb_feed();
    irq_restore(irq);
}

void sb_stop(void)
{
    int irq = irq_save();
    s_sb_left = 0;
    hw_wr(R_PCM_FLUSH, 1);
    irq_restore(irq);
}

/* PC speaker: PIT channel 2 at 1193182 Hz / divisor; silent below 41 like
 * the native mixer */
static uint32_t s_spk_half, s_spk_on;   /* save states */
void speaker_set_divisor(uint16_t div)
{
    s_spk_half = div > 40 ? (uint32_t)((uint64_t)div * s_sys_hz / (2u * 1193182u)) : 0;
    hw_wr(R_SPK_HALF, s_spk_half);
}
void speaker_gate(int on) { s_spk_on = on != 0; hw_wr(R_SPK_ON, s_spk_on); }
void snd_speaker_on(uint16_t div) { (void)div; speaker_gate(1); }
void snd_speaker_off(void) { speaker_gate(0); }
void snd_write_dac(uint8_t s) { (void)s; }
void snd_pause(void) {}
void snd_init(void) {}

static int s_volume = 70;
void sky_set_volume(int v)
{
    s_volume = v < 0 ? 0 : v > 100 ? 100 : v;
    hw_wr(R_VOLUME, (uint32_t)(s_volume * s_volume * 256 / 10000));
}
int sky_volume(void) { return s_volume; }

/* ------------------------------------------------------------------ */
/* interrupts                                                          */
/* ------------------------------------------------------------------ */
#ifdef POCKET_HOST
void host_irq_enable(int on);
int  host_irq_enabled(void);
static int irq_save(void) { int on = host_irq_enabled(); host_irq_enable(0); return on; }
static void irq_restore(int on) { host_irq_enable(on); }
#else
static int irq_save(void)
{
    uint32_t m;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(m) :: "memory");
    return (m & 8) != 0;
}
static void irq_restore(int on)
{
    if (on) __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
}
#endif

/* called from the trap entry (crt0.S) on an exception: report and stop */
void pocket_exception(uint32_t cause, uint32_t pc, uint32_t tval)
{
    fprintf(stderr, "exception %lu at pc %08lx (mtval %08lx)\n",
            (unsigned long)cause, (unsigned long)pc, (unsigned long)tval);
    plat_halt(128 + (int)(cause & 31));
}

#if defined(POCKET_SIM) && !defined(POCKET_HOST)
/* profile: the timer interrupt counts where it interrupted the game, in
 * 16-byte buckets of the firmware's code; printed at the end of the run
 * (sim/profile.py turns it into functions) */
#define PROF_BUCKETS (192 * 1024 / 16)
static uint16_t s_prof[PROF_BUCKETS];
static void prof_sample(void)
{
    uint32_t pc;
    __asm__ volatile ("csrr %0, mepc" : "=r"(pc));
    pc = (pc - SDRAM_BASE) / 16;
    if (pc < PROF_BUCKETS && s_prof[pc] < 0xffff) s_prof[pc]++;
}
static void prof_dump(void)
{
    unsigned i;
    for (i = 0; i < PROF_BUCKETS; i++)
        if (s_prof[i]) fprintf(stderr, "prof %08lx %u\n", (unsigned long)(SDRAM_BASE + i * 16), s_prof[i]);
}
#else
static void prof_sample(void) {}
static void prof_dump(void) {}
#endif

void ss_poll(void);
void ss_boot(void);
int  ss_excursion(void);
void ss_excursion_return(void);
void player_run(void);

/* called from the trap entry (crt0.S) on the timer interrupt */
void pocket_timer_irq(void)
{
    static unsigned bios_div;
    prof_sample();
    hw_wr(R_TIMER_ACK, 1);
#ifndef POCKET_HOST
    ss_poll();                          /* save states: savestate.c */
    /* the music player (a Core Setting), instead of the game while it is
     * on: the game is kept aside and comes back as it was (player.c) */
    if (hw_rd(R_SETTINGS) & 1) {
        if (ss_excursion() == 0) {
            player_run();
            ss_excursion_return();      /* does not return */
        }
    }
#endif
    if (s_timer_isr_on) timer_isr();
    else if (++bios_div >= 10) { bios_div = 0; s_bios_ticks++; }
    poll_buttons();
    sb_feed();
#ifdef POCKET_SIM
    {   /* once a second: how much of it the game spent waiting */
        static unsigned n;
        static uint64_t idle0;
        if (++n == 180) {
            n = 0;
            fprintf(stderr, "[%lu ms] idle %lu%%\n", (unsigned long)plat_millis(),
                    (unsigned long)((s_idle - idle0) * 100 / s_sys_hz));
            idle0 = s_idle;
        }
    }
#endif
}

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */
void plat_set_palette(int first, int count, const uint8_t *rgb)
{
    int i;
    if (count <= 0) return;
    if (first < 0) first = 0;
    if (first + count > 256) count = 256 - first;
    for (i = 0; i < count; i++) {
        uint8_t r = (uint8_t)((rgb[i * 3 + 0] & 0x3f) << 2 | (rgb[i * 3 + 0] & 0x3f) >> 4);
        uint8_t g = (uint8_t)((rgb[i * 3 + 1] & 0x3f) << 2 | (rgb[i * 3 + 1] & 0x3f) >> 4);
        uint8_t b = (uint8_t)((rgb[i * 3 + 2] & 0x3f) << 2 | (rgb[i * 3 + 2] & 0x3f) >> 4);
        g_palette[first + i][0] = r;
        g_palette[first + i][1] = g;
        g_palette[first + i][2] = b;
        hw_wr(R_PALETTE + (uint32_t)(first + i) * 4, (uint32_t)r << 16 | (uint32_t)g << 8 | b);
    }
}

/* the VGA memory is the screen: nothing to present */
void plat_present(void) {}
void plat_frame(void) {}
int  plat_pump_events(void) { return 1; }

/* 1000:5fc8 waits for the vertical retrace: the SoC counts 70 Hz retraces
 * (mode 13h's rate, whatever the Pocket's display runs at) */
void hw_wait_vsync(void)
{
    uint32_t start = hw_rd(R_RETRACE);
    uint64_t t0 = cycles(), give_up = t0 + s_sys_hz / 25;    /* 40 ms, like hw.c */
    while (hw_rd(R_RETRACE) == start && cycles() < give_up) { }
#ifdef POCKET_SIM
    s_idle += cycles() - t0;
#endif
}

/* not on the Pocket: widescreen, ghosts */
void plat_in_run(int on) { (void)on; }
void plat_set_wide(int on) { (void)on; }
void plat_file_opened(const char *name) { (void)name; }
void ghost_run_begin(void) {}
void ghost_run_end(void) {}
void ghost_frame(int16_t spr) { (void)spr; }
void ghost_publish(const uint8_t *depth) { (void)depth; }
void ghost_overlay(uint32_t *pixels, int pitch) { (void)pixels; (void)pitch; }

/* ------------------------------------------------------------------ */
/* mouse (INT 33h) and joystick (port 201h): the d-pad is the keyboard  */
/* ------------------------------------------------------------------ */
void FUN_1000_068b(void) {}
void FUN_1000_06a4(int x, int y) { (void)x; (void)y; }
int16_t FUN_1000_06b9(int what) { return (int16_t)what == 0 ? 320 : (int16_t)what == 1 ? 100 : 0; }
int16_t FUN_1000_05f6(int axis) { (void)axis; return 1000; }
uint8_t FUN_1000_0672(void) { return 0; }

/* ------------------------------------------------------------------ */
/* files: the game's files from the data pack, skyroads.cfg in save RAM */
/* ------------------------------------------------------------------ */
/* pack (tools/mkpack.py): "SKYPACK1", u32 count, then count entries of
 * { char name[16]; u32 offset; u32 size; }, offsets from the pack start */
typedef struct { char name[16]; uint32_t offset, size; } PackEntry;

/* save RAM: "SKY1", u32 length, then skyroads.cfg */
#define SAVE_MAGIC 0x31594b53u
#define CFG_MAX    (SAVE_SIZE - 8)

struct SkyFile {
    const uint8_t *data;
    uint32_t size, pos;
    int used, writing;
};
static SkyFile s_files[12];
static uint8_t s_cfg_buf[CFG_MAX];
static uint32_t s_cfg_len;              /* skyroads.cfg as last written, 0: not yet */

static void cfg_to_save_ram(void)
{
    uint8_t *s = hw_save();
    uint32_t magic = SAVE_MAGIC, len = s_cfg_len;
    memcpy(s + 8, s_cfg_buf, len);
    memcpy(s + 4, &len, 4);
    memcpy(s, &magic, 4);
}

/* lzs_fast.c's check: the files' state before and after a decode */
size_t pocket_files_size(void) { return sizeof s_files; }
void pocket_files_save(void *buf) { memcpy(buf, s_files, sizeof s_files); }
void pocket_files_restore(const void *buf) { memcpy(s_files, buf, sizeof s_files); }

static int name_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static SkyFile *new_file(void)
{
    unsigned i;
    for (i = 0; i < sizeof s_files / sizeof s_files[0]; i++)
        if (!s_files[i].used) {
            memset(&s_files[i], 0, sizeof s_files[i]);
            s_files[i].used = 1;
            return &s_files[i];
        }
    return NULL;
}

static int pack_ok(void)
{
    return memcmp(hw_pack(), "SKYPACK1", 8) == 0;
}

SkyFile *sky_open(const char *name)
{
    SkyFile *f;
#ifdef POCKET_SIM
    fprintf(stderr, "[%lu ms] open %s\n", (unsigned long)plat_millis(), name);
#endif
    if (name_eq(name, "skyroads.cfg")) {
        const uint8_t *s = hw_save();
        uint32_t magic, len;
        memcpy(&magic, s, 4);
        memcpy(&len, s + 4, 4);
        if (magic != SAVE_MAGIC || len == 0 || len > CFG_MAX) return NULL;
        if (!(f = new_file())) return NULL;
        f->data = s + 8;
        f->size = len;
#ifdef POCKET_SIM
        fprintf(stderr, "save: %lu bytes, controls %u, sound %u, road 1 done %u times\n",
                (unsigned long)len, s[10] | s[11] << 8, s[12] | s[13] << 8, s[14] | s[15] << 8);
#endif
        return f;
    }
    if (pack_ok()) {
        const uint8_t *p = hw_pack();
        uint32_t n, i;
        memcpy(&n, p + 8, 4);
        for (i = 0; i < n && i < 64; i++) {
            PackEntry e;
            memcpy(&e, p + 12 + i * sizeof e, sizeof e);
            e.name[15] = 0;
            if (name_eq(e.name, name)) {
                if (!(f = new_file())) return NULL;
                f->data = p + e.offset;
                f->size = e.size;
                return f;
            }
        }
    }
    return NULL;
}

/* the game only creates skyroads.cfg */
SkyFile *sky_create(const char *name)
{
    SkyFile *f;
    if (!name_eq(name, "skyroads.cfg") || !(f = new_file())) return NULL;
    f->writing = 1;
    f->data = s_cfg_buf;
    return f;
}

int sky_read(SkyFile *f, void *dst, int len)
{
    uint32_t n;
    if (f->writing || len <= 0) return 0;
    n = f->size - f->pos;
    if ((uint32_t)len < n) n = (uint32_t)len;
    memcpy(dst, f->data + f->pos, n);
    f->pos += n;
    return (int)n;
}

int sky_write(SkyFile *f, const void *src, int len)
{
    uint32_t n;
    if (!f->writing || len <= 0) return 0;
    n = CFG_MAX - f->pos;
    if ((uint32_t)len < n) n = (uint32_t)len;
    memcpy(s_cfg_buf + f->pos, src, n);
    f->pos += n;
    if (f->pos > f->size) f->size = f->pos;
    return (int)n;
}

long sky_seek(SkyFile *f, long off, int whence)
{
    long base = whence == 2 ? (long)f->size : whence == 1 ? (long)f->pos : 0;
    long p = base + off;
    if (p < 0) return -1;
    f->pos = (uint32_t)p;
    return p;
}

long sky_tell(SkyFile *f) { return (long)f->pos; }
int  sky_eof(SkyFile *f) { return f->pos >= f->size; }

void sky_close(SkyFile *f)
{
    if (!f) return;
    if (f->writing) {                   /* skyroads.cfg: into the save RAM */
        s_cfg_len = f->size;
        cfg_to_save_ram();
#ifdef POCKET_SIM
        {
            const uint8_t *r = hw_save();
            fprintf(stderr, "save: skyroads.cfg written, %lu bytes; save RAM reads %c%c%c%c, %u bytes, road 1 done %u times\n",
                    (unsigned long)f->size, r[0], r[1], r[2], r[3], r[4] | r[5] << 8, r[14] | r[15] << 8);
        }
#endif
    }
    f->used = 0;
}

/* ------------------------------------------------------------------ */
/* start and end                                                       */
/* ------------------------------------------------------------------ */

/* DOS "terminate program" (1000:5fc3) */
void game_exit(int code) { prof_dump(); plat_halt(code & 0xff); }
#ifndef POCKET_HOST
void _exit(int code) { prof_dump(); plat_halt(code); }   /* render.c's demo test */
#endif

int plat_init(void) { return 0; }
void plat_shutdown(void) {}

static uint8_t s_mem_raw[MEM_SIZE + 0x20000];

/* save states (savestate.c): the hardware the CPU cannot read back */
static uint8_t s_vram_copy[65536];
static uint32_t timer_period(void) { return (uint32_t)((uint64_t)s_sys_hz * 6628u / 1193182u); }

void ss_platform_save(void)
{
    s_cyc_saved = cycles();
    memcpy(s_vram_copy, g_mem + VGA_BASE, sizeof s_vram_copy);
}

void ss_platform_restore(void)
{
    unsigned i;
    s_cyc_off = s_cyc_saved - raw_cycles();
    hw_wr(R_VRAM_BASE, (uint32_t)(uintptr_t)(g_mem + VGA_BASE));
    memcpy(g_mem + VGA_BASE, s_vram_copy, sizeof s_vram_copy);
    for (i = 0; i < 256; i++)
        hw_wr(R_PALETTE + i * 4, (uint32_t)g_palette[i][0] << 16 | (uint32_t)g_palette[i][1] << 8 | g_palette[i][2]);
    opl_mute = 0;
    opl_replay();                       /* the OPL2 */
    sky_set_volume(s_volume);
    hw_wr(R_SPK_HALF, s_spk_half);
    hw_wr(R_SPK_ON, s_spk_on);
    s_sb_left = 0;                      /* a sound effect being played: dropped */
    hw_wr(R_PCM_FLUSH, 1);
    hw_wr(R_TIMER_PERIOD, timer_period());
    /* the save RAM is not in the save state: if the game wrote skyroads.cfg
     * before the state was saved, write it again, so the save file (the
     * road completions) agrees with the game that carries on */
    if (s_cfg_len) cfg_to_save_ram();
}

static void show_error(void)
{
    /* no data pack: a red screen */
    hw_wr(R_PALETTE, 0xc00000);
    memset(g_mem + VGA_BASE, 0, 320 * 200);
    for (;;) { }
}

#ifdef POCKET_HOST
#define main pocket_main             /* host_hal.c has the real main */
#endif
int main(void)
{
    uintptr_t ds;
    s_sys_hz = hw_rd(R_SYS_HZ);
    /* the data segment (g_mem + DS_BASE) 64 KiB aligned, see compat.h */
    ds = ((uintptr_t)s_mem_raw + DS_BASE + 0xffff) & ~(uintptr_t)0xffff;
    g_mem = (uint8_t *)(ds - DS_BASE);
    memcpy(g_mem + CS_BASE, skyroads_image, sizeof(skyroads_image));
    g_ds = g_mem + DS_BASE;
    hw_wr(R_VRAM_BASE, (uint32_t)(uintptr_t)(g_mem + VGA_BASE));
    sky_set_volume(s_volume);
#ifdef POCKET_SIM
    fprintf(stderr, "pocket: firmware started, %lu Hz\n", (unsigned long)s_sys_hz);
#endif
    if (!pack_ok()) show_error();
    /* the PIT rate the game sets (divisor 0x19e4, ~180 Hz) */
    hw_wr(R_TIMER_PERIOD, timer_period());
#ifndef POCKET_HOST
    ss_boot();                          /* waking from sleep: does not return */
#endif
    irq_restore(1);
    entry();
    plat_halt(0);
    return 0;
}
