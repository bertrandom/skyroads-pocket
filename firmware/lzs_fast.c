/* LZS decompression (1000:6660) for the Pocket: the same algorithm as
 * port/runtime.c's, with the game's bit reader kept in registers.
 *
 * The shared decoder reads every bit through FUN_1000_6425, which loads and
 * stores the reader's state (current byte, bits left, buffer pointer) in the
 * DOS data segment each time: on the Pocket's CPU that made decoding the
 * game's files most of its loading time.  Here the state lives in locals
 * and goes back to the data segment only when the buffer has to be refilled
 * from the file (FUN_1000_62ca, which reads and updates it) and at the end.
 *
 * Built with POCKET_LZS_CHECK (the host test build), every call also runs
 * the shared decoder (sky_lzs_reference) from the same state and stops if
 * the DOS memory or the files differ afterwards. */
#include "compat.h"
#include "protos.h"

#define DS16(off) (*(uint16_t *)(g_ds + (off)))
#define DS8(off)  (*(uint8_t  *)(g_ds + (off)))
#define RD_BITS   DS16(0x41ba)
#define RD_BYTE   DS8(0x41bc)
#define RD_END    DS16(0x41c0)
#define RD_PTR    DS16(0x41c2)

/* the reader's state, in registers */
typedef struct { uint32_t byte, bits; uint16_t ptr, end; } Reader;

static inline void rd_load(Reader *r)
{
    r->byte = RD_BYTE; r->bits = RD_BITS; r->ptr = RD_PTR; r->end = RD_END;
}
static inline void rd_store(const Reader *r)
{
    RD_BYTE = (uint8_t)r->byte; RD_BITS = (uint16_t)r->bits; RD_PTR = r->ptr;
}

/* FUN_1000_6425: the next bit, most significant first */
static inline uint32_t rd_bit(Reader *r)
{
    uint32_t bit = (r->byte >> 7) & 1;
    r->byte = (r->byte << 1) & 0xff;
    if (--r->bits == 0) {
        r->bits = 8;
        if (r->ptr == r->end) {         /* refill from the file */
            rd_store(r);
            FUN_1000_62ca();
            rd_load(r);
        } else r->byte = g_ds[r->ptr++];
    }
    return bit;
}

static inline uint32_t rd_bits(Reader *r, uint32_t n)
{
    uint32_t v = 0;
    while (n--) v = (v << 1) | rd_bit(r);
    return v;
}

static void lzs_fast(int _dest_off, int _dest_seg, int _size)
{
    uint16_t dest_off = (uint16_t)_dest_off;
    uint16_t dest_seg = (uint16_t)_dest_seg;
    uint16_t size = (uint16_t)_size;
    uint8_t  *base = g_mem + ((uint32_t)dest_seg << 4);
    uint32_t  di = dest_off;
    uint32_t  end = (uint32_t)dest_off + size;
    uint32_t  len_bits, off_bits, long_bits, long_add;
    Reader    r;

    len_bits  = FUN_1000_640a();        /* the header: three whole bytes */
    off_bits  = FUN_1000_640a();
    long_add  = 1u << (off_bits & 0x1F);
    long_bits = FUN_1000_640a();

    rd_load(&r);
    while (di < end) {
        uint32_t offset, len, count, si;
        if (rd_bit(&r) == 0)
            offset = rd_bits(&r, off_bits);
        else if (rd_bit(&r) == 0)
            offset = rd_bits(&r, long_bits) + long_add;
        else {
            uint32_t lit = rd_bits(&r, 8);
            if (di < end) base[di++] = (uint8_t)lit;
            continue;
        }
        len = rd_bits(&r, len_bits);
        if (len + 1 >= end - di) break;
        si = di - 2 - offset;
        count = len + 2;
        while (count-- != 0 && di < end) base[di++] = base[si++];
    }
    rd_store(&r);
    FUN_1000_65f3();                    /* byte-align the stream */
}

#ifndef POCKET_LZS_CHECK
void FUN_1000_6660(int dest_off, int dest_seg, int size)
{
    lzs_fast(dest_off, dest_seg, size);
}
#else
/* pocket.c: the open files' state, to run both decoders from the same one */
size_t pocket_files_size(void);
void   pocket_files_save(void *buf);
void   pocket_files_restore(const void *buf);
void   sky_lzs_reference(int dest_off, int dest_seg, int size);
void   host_irq_enable(int on);         /* host_hal.c: the timer interrupt */
int    host_irq_enabled(void);

void FUN_1000_6660(int dest_off, int dest_seg, int size)
{
    static uint8_t *before, *after;
    static unsigned calls;
    uint8_t fbefore[1024], fafter[1024];
    size_t fn = pocket_files_size();
    if (!before) { before = malloc(MEM_SIZE); after = malloc(MEM_SIZE); }
    int irq = host_irq_enabled();
    if (fn > sizeof fbefore) abort();
    host_irq_enable(0);                 /* the music changes the DS meanwhile */
    memcpy(before, g_mem, MEM_SIZE);
    pocket_files_save(fbefore);
    sky_lzs_reference(dest_off, dest_seg, size);
    memcpy(after, g_mem, MEM_SIZE);
    pocket_files_save(fafter);
    memcpy(g_mem, before, MEM_SIZE);
    pocket_files_restore(fbefore);
    lzs_fast(dest_off, dest_seg, size);
    calls++;
    if (memcmp(g_mem, after, MEM_SIZE) != 0) {
        uint32_t i = 0;
        while (g_mem[i] == after[i]) i++;
        fprintf(stderr, "lzs check: call %u differs at %05x\n", calls, (unsigned)i);
        abort();
    }
    pocket_files_save(fbefore);
    if (memcmp(fbefore, fafter, fn) != 0) {
        fprintf(stderr, "lzs check: call %u leaves the files differently\n", calls);
        abort();
    }
    host_irq_enable(irq);
    fprintf(stderr, "lzs check: call %u (%u bytes) identical\n", calls, (unsigned)(uint16_t)size);
}
#endif
