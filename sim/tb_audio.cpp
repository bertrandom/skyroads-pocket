// Unit test for rtl/audio.v's OPL resampling: one pure tone on the OPL2,
// sampled at 48 kHz (as the I2S side does) from the chip's raw output
// (what the core played before the resampler) and from the resampled one;
// writes both as s16le mono for tools/spurs.py.
//   Vaudio OUT_RAW OUT_RESAMPLED
#include "Vaudio.h"
#include "Vaudio___024root.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
static Vaudio *top;
static void tick() { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
static void opl(int reg, int val)
{
    top->opl_data = reg << 8 | val; top->opl_wr = 1; tick(); top->opl_wr = 0;
    for (int i = 0; i < 2000; i++) tick();
}
int main(int argc, char **argv)
{
    const double SYS = 49152000, OUT = 48000;
    top = new Vaudio;
    top->rst_n = 0; top->volume = 256; tick(); tick(); top->rst_n = 1;
    for (int i = 0; i < 200000; i++) tick();            // the OPL's reset
    opl(0x01, 0x20);                                    // waveform select on
    opl(0x20, 0x21); opl(0x23, 0x21);                   // sustained, multiple 1
    opl(0x40, 0x3f); opl(0x43, 0x00);                   // modulator silent, carrier loud
    opl(0x60, 0xf0); opl(0x63, 0xf0); opl(0x80, 0x00); opl(0x83, 0x00);
    opl(0xc0, 0x01);
    opl(0xa0, 0xff); opl(0xb0, 0x20 | 6 << 2 | 3);      // fnum 1023, block 6: 3104 Hz, key on
    for (int i = 0; i < 4900000; i++) tick();           // 0.1 s to settle
    FILE *fa = fopen(argv[1], "wb"), *fb = fopen(argv[2], "wb");
    double ph = 0;
    for (long i = 0; i < (long)(SYS * 1.0); i++) {      // 1 s
        tick();
        ph += OUT / SYS;
        if (ph >= 1) {
            ph -= 1;
            int16_t a = (int16_t)top->rootp->audio__DOT__opl_snd, b = (int16_t)top->rootp->audio__DOT__opl_out;
            fwrite(&a, 2, 1, fa); fwrite(&b, 2, 1, fb);
        }
    }
    fclose(fa); fclose(fb);
    delete top;
    return 0;
}
