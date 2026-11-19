/* Save states, for the Pocket's sleep (and its Memories).
 *
 * The machine is the firmware's RAM plus a little hardware.  A save state
 * ("blob") is the RAM that changes, from .data to the heap's end and the
 * stack in use, copied into the SDRAM's staging area (STAGE_BASE), which
 * the Pocket reads over the bridge; the hardware that the CPU cannot read
 * back (the VRAM is copied into RAM first; the palette, the OPL2's
 * registers, the speaker, the volume) is put back from what pocket.c kept.
 *
 * Saving happens in the timer interrupt: ss_setjmp records where, and the
 * RAM is copied with that interrupt's frame on the stack.  Loading copies
 * the RAM back from the staging area (ss_resume, in crt0.S: no stack while
 * the stack is overwritten) and returns a second time from that
 * ss_setjmp, inside the interrupt that saved: the interrupt then finishes
 * and returns into the game where it was.  A load is taken at start-up
 * (waking from sleep: the core has just been loaded) or in the timer
 * interrupt (a Memory loaded while the game runs).
 *
 * The blob is only good for the same firmware: its header has a hash of
 * the code and constant data, and a load with another one is ignored. */
#include "compat.h"
#include "pocket_hw.h"

#define SS_MAGIC   0x53534b53u          /* 'SKSS' */
#define SS_VERSION 1u
#define SS_NREG    3

typedef struct { uint32_t dst, len, src; } SsRegion;
typedef struct {
    uint32_t magic, version, build, nreg;
    SsRegion reg[SS_NREG];
    uint32_t total;
} SsHeader;
typedef struct { uint32_t r[16]; } SsCtx;

int  ss_setjmp(SsCtx *ctx);                                     /* crt0.S */
void ss_resume(const SsRegion *r, uint32_t n, SsCtx *ctx);      /* crt0.S */
void ss_platform_save(void);                                    /* pocket.c */
void ss_platform_restore(void);
char *libc_brk(void);

extern char _start[], __data_start[], _end[], _stack_top[];

static SsCtx    s_ctx;
static uint32_t s_mepc, s_mstatus;

/* FNV-1a over the code and constant data, a word at a time */
static uint32_t build_hash(void)
{
    const uint32_t *p = (const uint32_t *)(uintptr_t)_start;
    const uint32_t *e = (const uint32_t *)(uintptr_t)__data_start;
    uint32_t h = 2166136261u;
    while (p < e) h = (h ^ *p++) * 16777619u;
    return h;
}

/* write the data cache back: it is direct mapped, 32 KiB of 16-byte
 * lines, so reading 32 KiB of the (read-only) data pack replaces every
 * line, writing back the dirty ones */
static void dcache_evict(void)
{
    const volatile uint32_t *p = (const volatile uint32_t *)(uintptr_t)PACK_BASE;
    uint32_t i;
    for (i = 0; i < 32768 / 4; i += 4) (void)p[i];
}

static void resumed(void)
{
    __asm__ volatile ("csrw mepc, %0" :: "r"(s_mepc));
    __asm__ volatile ("csrw mstatus, %0" :: "r"(s_mstatus));
    ss_platform_restore();
}

/* the changing RAM into a blob at base (a header, then the regions):
 * .data to the heap's end, and the stack from here up (this function's
 * callers included: noinline, so that its frame is below theirs) */
static __attribute__((noinline)) uint32_t snapshot(uint32_t base)
{
    SsHeader *h = (SsHeader *)(uintptr_t)base;
    uint32_t sp, at, i;
    __asm__ volatile ("mv %0, sp" : "=r"(sp));
    sp &= ~15u;
    h->reg[0].dst = (uint32_t)(uintptr_t)__data_start;
    h->reg[0].len = (uint32_t)((uintptr_t)_end - (uintptr_t)__data_start + 3) & ~3u;
    h->reg[1].dst = ((uint32_t)(uintptr_t)_end + 3) & ~3u;
    h->reg[1].len = (uint32_t)((uintptr_t)libc_brk() - h->reg[1].dst + 3) & ~3u;
    h->reg[2].dst = sp;
    h->reg[2].len = (uint32_t)(uintptr_t)_stack_top - sp;
    at = (sizeof *h + 15) & ~15u;
    for (i = 0; i < SS_NREG; i++) {
        h->reg[i].src = base + at;
        memcpy((void *)(uintptr_t)h->reg[i].src, (const void *)(uintptr_t)h->reg[i].dst, h->reg[i].len);
        at = (at + h->reg[i].len + 15) & ~15u;
    }
    h->magic = SS_MAGIC;
    h->version = SS_VERSION;
    h->build = build_hash();
    h->nreg = SS_NREG;
    h->total = at;
    return at;
}

/* in the timer interrupt (interrupts off) */
static void ss_save(void)
{
    uint32_t total;
    ss_platform_save();
    __asm__ volatile ("csrr %0, mepc" : "=r"(s_mepc));
    __asm__ volatile ("csrr %0, mstatus" : "=r"(s_mstatus));
    if (ss_setjmp(&s_ctx)) {            /* the second return: a load */
        resumed();
        return;
    }
    total = snapshot(STAGE_BASE);
    dcache_evict();                     /* the bridge reads the SDRAM */
    hw_wr(R_SS_SIZE, total);
    hw_wr(R_SS_CTRL, 1);
}

/* An excursion (the music player): the game is kept at EXC_BASE, as a save
 * state that never leaves the SDRAM; ss_excursion() returns 0 then, and 1
 * when ss_excursion_return() has put the game back (inside the same timer
 * interrupt: it returns into the game when that interrupt ends). */
static SsCtx    s_xctx;
static uint32_t s_xmepc, s_xmstatus;

int ss_excursion(void)
{
    ss_platform_save();
    __asm__ volatile ("csrr %0, mepc" : "=r"(s_xmepc));
    __asm__ volatile ("csrr %0, mstatus" : "=r"(s_xmstatus));
    if (ss_setjmp(&s_xctx)) {
        __asm__ volatile ("csrw mepc, %0" :: "r"(s_xmepc));
        __asm__ volatile ("csrw mstatus, %0" :: "r"(s_xmstatus));
        ss_platform_restore();
        return 1;
    }
    snapshot(EXC_BASE);
    return 0;
}

void ss_excursion_return(void)
{
    const SsHeader *h = (const SsHeader *)(uintptr_t)EXC_BASE;
    ss_resume(h->reg, SS_NREG, &s_xctx);
}

/* during an excursion: a save state (sleep) is refused */
void ss_refuse(void)
{
    if (hw_rd(R_SS_STATUS) & 1) hw_wr(R_SS_CTRL, 2);
}

static int region_ok(const SsRegion *r)
{
    uint32_t ram0 = (uint32_t)(uintptr_t)__data_start, ram1 = (uint32_t)(uintptr_t)_stack_top;
    return r->len % 4 == 0 && r->dst % 4 == 0 && r->src % 4 == 0 &&
           r->dst >= ram0 && r->dst <= ram1 && r->len <= ram1 - r->dst &&
           r->src >= STAGE_BASE && r->len <= STAGE_SIZE && r->src - STAGE_BASE <= STAGE_SIZE - r->len;
}

/* a blob was written to the staging area: put it back and carry on from
 * where it was saved (does not return), or ignore it */
static void ss_load(void)
{
    const SsHeader *h = (const SsHeader *)(uintptr_t)STAGE_BASE;
    uint32_t i;
    hw_wr(R_SS_CTRL, 4);
    dcache_evict();                     /* nothing stale of the staging area */
    if (h->magic != SS_MAGIC || h->version != SS_VERSION || h->nreg != SS_NREG ||
        h->build != build_hash())
        return;
    for (i = 0; i < SS_NREG; i++)
        if (!region_ok(&h->reg[i])) return;
    ss_resume(h->reg, SS_NREG, &s_ctx);
}

/* pocket.c: in every timer interrupt */
void ss_poll(void)
{
    uint32_t st = hw_rd(R_SS_STATUS);
    if (st & 2) ss_load();
    if (st & 1) ss_save();
}

/* pocket.c: at start-up, before the game (a save request waits for the
 * timer interrupt, which is where a save must be made) */
void ss_boot(void)
{
    if (hw_rd(R_SS_STATUS) & 2) ss_load();
}
