/* Native test build of the Pocket firmware: the SoC's registers simulated
 * on the host, so the platform layer (pocket.c) can be run and checked
 * without the hardware.  SIGALRM stands in for the timer interrupt.
 *
 *   POCKET_PACK=file      the data pack (default ../build/skyroads_data.bin)
 *   POCKET_SAVE=file      save RAM contents, read at start, written at exit
 *   POCKET_SHOT=dir       write the screen as dir/shot_<ms>.ppm at the
 *   POCKET_SHOT_AT=ms,..  listed times
 *   POCKET_KEYS=ms:hex,.. set the controller buttons (cont1_key) at times
 *   SKY_DEMO_TEST=1       render.c: replay the demo, exit 0 when finished
 */
#ifndef POCKET_HOST
#define POCKET_HOST                     /* this file is only the host build */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include "pocket_hw.h"

void pocket_timer_irq(void);
extern uint8_t *g_mem;

#define HZ 1000000u                     /* "cycles" are microseconds */

static uint8_t s_save[SAVE_SIZE];
static uint8_t *s_pack;
static uint32_t s_palette[256], s_vram_base, s_period;
static uint64_t s_t0;
static uint32_t s_opl_writes, s_pcm_samples;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u - s_t0;
}

uint8_t *hw_pack(void) { return s_pack; }
uint8_t *hw_save(void) { return s_save; }

static void on_alarm(int sig) { (void)sig; pocket_timer_irq(); }

static sigset_t alarm_set(void)
{
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGALRM);
    return s;
}
static int s_irq_on;
void host_irq_enable(int on)
{
    sigset_t s = alarm_set();
    s_irq_on = on;
    sigprocmask(on ? SIG_UNBLOCK : SIG_BLOCK, &s, NULL);
}
int host_irq_enabled(void) { return s_irq_on; }

static void set_timer(uint32_t period_us)
{
    struct itimerval it;
    memset(&it, 0, sizeof it);
    it.it_interval.tv_usec = (suseconds_t)period_us;
    it.it_value.tv_usec = (suseconds_t)period_us;
    setitimer(ITIMER_REAL, &it, NULL);
}

static void shot(uint64_t ms)
{
    const char *dir = getenv("POCKET_SHOT");
    char path[512];
    FILE *fp;
    const uint8_t *fb = g_mem ? g_mem + 0xA0000 : NULL;   /* the VRAM window (a 32-bit register: a host pointer does not fit) */
    int i;
    if (!dir || !fb || !s_vram_base) return;
    snprintf(path, sizeof path, "%s/shot_%06llu.ppm", dir, (unsigned long long)ms);
    if (!(fp = fopen(path, "wb"))) return;
    fprintf(fp, "P6 320 200 255\n");
    for (i = 0; i < 320 * 200; i++) {
        uint32_t c = s_palette[fb[i]];
        fputc((int)(c >> 16) & 255, fp); fputc((int)(c >> 8) & 255, fp); fputc((int)c & 255, fp);
    }
    fclose(fp);
}

/* screenshots at the listed times, checked when the timer interrupt polls
 * the buttons */
static void shots_due(void)
{
    static const char *next;
    static int init;
    uint64_t ms = now_us() / 1000u;
    if (!init) { init = 1; next = getenv("POCKET_SHOT_AT"); }
    while (next && *next) {
        char *end;
        unsigned long at = strtoul(next, &end, 10);
        if (ms < at) break;
        shot(at);
        next = *end == ',' ? end + 1 : NULL;
    }
}

static uint32_t keys_now(void)
{
    static const char *next;
    static int init;
    static uint32_t keys;
    uint64_t ms = now_us() / 1000u;
    if (!init) { init = 1; next = getenv("POCKET_KEYS"); }
    while (next && *next) {
        char *end;
        unsigned long at = strtoul(next, &end, 10);
        if (ms < at || *end != ':') break;
        keys = (uint32_t)strtoul(end + 1, &end, 16);
        next = *end == ',' ? end + 1 : NULL;
    }
    return keys;
}

uint32_t hw_rd(uint32_t reg)
{
    static uint64_t latched;
    switch (reg) {
        case R_ID: return 0x31594b53u;
        case R_SYS_HZ: return HZ;
        case R_CYCLES_LO: latched = now_us(); return (uint32_t)latched;
        case R_CYCLES_HI: return (uint32_t)(latched >> 32);
        case R_RETRACE: return (uint32_t)(now_us() * 70086u / 1000000000u);
        case R_KEYS: shots_due(); return keys_now();   /* polled at 180 Hz */
        case R_OPL_FREE: return 512;
        case R_PCM_FREE: return 64;
        case R_TIMER_PERIOD: return s_period;
        case R_VRAM_BASE: return s_vram_base;
        default: return 0;
    }
}

void hw_wr(uint32_t reg, uint32_t val)
{
    if (reg >= R_PALETTE && reg < R_PALETTE + 1024) { s_palette[(reg - R_PALETTE) / 4] = val; return; }
    switch (reg) {
        case R_TIMER_PERIOD: s_period = val; set_timer(val); break;
        case R_VRAM_BASE: s_vram_base = val; break;
        case R_OPL: s_opl_writes++; break;
        case R_PCM: s_pcm_samples++; break;
        case R_DEBUG_PUTC: fputc((int)val, stderr); break;
        default: break;
    }
}

void plat_halt(int code)
{
    const char *save = getenv("POCKET_SAVE");
    FILE *fp;
    if (save && (fp = fopen(save, "wb"))) { fwrite(s_save, 1, sizeof s_save, fp); fclose(fp); }
    fprintf(stderr, "pocket host: exit %d (%u OPL writes, %u samples)\n", code, s_opl_writes, s_pcm_samples);
    exit(code);
}

int pocket_main(void);

int main(void)
{
    const char *pack = getenv("POCKET_PACK"), *save = getenv("POCKET_SAVE");
    FILE *fp;
    long n;
    s_t0 = 0; s_t0 = now_us();
    if (!pack) pack = "../build/skyroads_data.bin";
    if (!(fp = fopen(pack, "rb"))) { perror(pack); return 1; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    s_pack = malloc((size_t)n);
    if (fread(s_pack, 1, (size_t)n, fp) != (size_t)n) { perror(pack); return 1; }
    fclose(fp);
    if (save && (fp = fopen(save, "rb"))) { if (fread(s_save, 1, sizeof s_save, fp)) {} fclose(fp); }
    host_irq_enable(0);
    signal(SIGALRM, on_alarm);
    return pocket_main();
}
