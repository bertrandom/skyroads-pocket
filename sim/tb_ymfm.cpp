// The game's songs through ymfm's YM3812 (Aaron Giles' emulation of the OPL2,
// vendor/ymfm): a reference to set the Pocket's renders (tb_songs.cpp,
// jtopl2) and the native port's (Nuked-OPL3) against.
//
// The same writes as tb_songs.cpp (../decomp/port/tools/render_songs.c --writes),
// each tick's at that tick; the chip's own rate (3579545 / 72 = 49716 Hz)
// with no resampling and no mixer: the chip's output as it is, faded over
// the last 3 seconds like the other renders.
//
//   ymfm_songs IN.opl OUT.wav
#include "ymfm_opl.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

static const double CLK = 3579545.0, PIT = 1193182.0, DIV = 6628.0;

static void wav_header(FILE *f, uint32_t rate, uint32_t samples)
{
    uint32_t v, data = samples * 2;
    uint16_t fmt = 1, ch = 1, align = 2, bps = 16;
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: ymfm_songs IN.opl OUT.wav\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    uint32_t total = 0, len = 0, w;
    if (fread(&total, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1) { fprintf(stderr, "%s: short\n", argv[1]); return 1; }
    std::vector<uint32_t> writes;
    while (fread(&w, 4, 1, f) == 1) writes.push_back(w);
    fclose(f);

    ymfm::ymfm_interface intf;
    ymfm::ym3812 chip(intf);
    chip.reset();
    const uint32_t rate = chip.sample_rate((uint32_t)CLK);

    FILE *o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    wav_header(o, rate, 0);
    const double out_per_tick = CLK / 72.0 * DIV / PIT;
    const uint32_t fade = (uint32_t)(3 * PIT / DIV);
    double next_tick = 0;
    uint32_t samples = 0, peak = 0;
    size_t wi = 0;
    for (uint32_t t = 0; t < total; t++) {
        while (wi < writes.size() && (writes[wi] >> 16) <= t) {
            chip.write_address((writes[wi] >> 8) & 0xff);
            chip.write_data(writes[wi] & 0xff);
            wi++;
        }
        double fg = t < len ? 1.0 : 1.0 - (double)(t - len) / fade;
        if (fg < 0) fg = 0;
        next_tick += out_per_tick;
        for (; samples < next_tick; samples++) {
            ymfm::ym3812::output_data out;
            chip.generate(&out);
            int v = (int)(out.data[0] * fg);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            if ((uint32_t)std::abs(v) > peak) peak = (uint32_t)std::abs(v);
            int16_t s = (int16_t)v;
            fwrite(&s, 2, 1, o);
        }
    }
    fseek(o, 0, SEEK_SET);
    wav_header(o, rate, samples);
    fclose(o);
    printf("%s: %u samples at %u Hz, peak %.1f%%, %zu of %zu writes\n", argv[2], samples, rate, peak * 100.0 / 32767, wi, writes.size());
    return 0;
}
