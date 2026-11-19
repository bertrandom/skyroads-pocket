// The game's songs through the Pocket's audio hardware (rtl/audio.v: the
// OPL write queue, jtopl2, the resampler, the mixer), as WAV files: what the
// core sends to the Pocket's DAC.
//
// The writes are the ones the firmware makes, tick by tick (the shared
// player, music.c, saved by ../decomp/port/tools/render_songs.c --writes); each
// tick's writes go into the queue at that tick, as opl_out() does (waiting
// while it is full).  The mixer's volume is the firmware's default (70:
// 70 * 70 * 256 / 10000).  The output is sampled at 48 kHz, as the I2S side
// takes it, and faded over the last 3 seconds like the native renders.
//
// audio.v runs here with its system clock at the OPL2's 3.579545 MHz
// (SYS_HZ, set when verilating) instead of the Pocket's 49.152 MHz: jtopl2
// steps on its clock enable, which is then every cycle, so it computes the
// same samples; only the instants at which the resampler and the 48 kHz
// side pick them up move by a fraction of a microsecond.  14 times faster.
//
//   Vaudio_songs IN.opl OUT.wav
#include "Vaudio.h"
#include "Vaudio___024root.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

static const double SYS = 3579545.0, OUT = 48000.0, PIT = 1193182.0, DIV = 6628.0;
static Vaudio *top;
static void tick() { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }

static void wav_header(FILE *f, uint32_t samples)
{
    uint32_t v, data = samples * 2;
    uint16_t fmt = 1, ch = 1, align = 2, bps = 16;
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    v = (uint32_t)OUT; fwrite(&v, 4, 1, f); v = (uint32_t)OUT * 2; fwrite(&v, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: Vaudio_songs IN.opl OUT.wav\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    uint32_t total = 0, len = 0, w;
    if (fread(&total, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1) { fprintf(stderr, "%s: short\n", argv[1]); return 1; }
    std::vector<uint32_t> writes;
    while (fread(&w, 4, 1, f) == 1) writes.push_back(w);
    fclose(f);

    top = new Vaudio;
    top->rst_n = 0; top->volume = 70 * 70 * 256 / 10000; top->pcm_period = 1000;
    tick(); tick(); top->rst_n = 1;
    for (int i = 0; i < 50000; i++) tick();     // the OPL's reset

    FILE *o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    wav_header(o, 0);
    const double cyc_per_tick = SYS * DIV / PIT, cyc_per_out = SYS / OUT;
    const uint32_t fade = (uint32_t)(3 * PIT / DIV);
    double next_tick = 0, next_out = 0;
    uint64_t cyc = 0;
    uint32_t t = 0, samples = 0, peak = 0;
    size_t wi = 0;
    bool pending = false;
    while (t < total) {
        // this tick's writes into the queue, one per cycle while there is room
        if (cyc >= next_tick) { pending = true; next_tick += cyc_per_tick; t++; }
        top->opl_wr = 0;
        if (pending) {
            if (wi < writes.size() && (writes[wi] >> 16) < t) {
                if (top->opl_free > 1) { top->opl_data = writes[wi] & 0xffff; top->opl_wr = 1; wi++; }
            } else pending = false;
        }
        tick();
        cyc++;
        if (cyc >= next_out) {
            next_out += cyc_per_out;
            double fg = t <= len ? 1.0 : 1.0 - (double)(t - len) / fade;
            if (fg < 0) fg = 0;
            int16_t raw = (int16_t)top->sample_out;     // Verilator: the bits, unsigned
            int16_t s = (int16_t)(raw * fg);
            if ((uint32_t)std::abs(s) > peak) peak = (uint32_t)std::abs(s);
            fwrite(&s, 2, 1, o);
            samples++;
        }
    }
    fseek(o, 0, SEEK_SET);
    wav_header(o, samples);
    fclose(o);
    printf("%s: %u samples, peak %.1f%%, %zu of %zu writes\n", argv[2], samples, peak * 100.0 / 32767, wi, writes.size());
    delete top;
    return 0;
}
