// Verilator testbench for the whole core (fpga/core_top.v with the
// template's bridge command handler), playing the Pocket's part:
//
//   - the data slots are written over the bridge, as big-endian words at
//     the addresses of data.json: firmware at 0, the data pack at 0x800000,
//     a save file at 0x20000000;
//   - then the host commands [008F Data slot access all complete] and
//     [0011 Reset Exit], after which the CPU must start and run the game
//     from what the loader put in the SDRAM;
//   - at the end, [0010 Reset Enter] and the save read back over the bridge
//     as the Pocket does on exit, and the data slot table's entry for it.
//
// The PLL and DDIO are stand-ins (sim/lint/altera_stubs.v): every clock is
// clk_74a, so this checks the loader, the byte order, the save path and the
// run control, not the clock crossings' timing.
//
// With --sleep, a sleep and wake as the Pocket does it: the game runs, the
// Pocket asks for a save state [00A0], reads it over the bridge, and the
// core is powered off (a new model, an empty SDRAM); the core is loaded
// again, the save state written to the address [00A0] gave, [00A4], and
// the game must carry on from where it was: its frames after waking are
// compared with the frames the first run went on to show.  The state is
// saved on the main menu (Start pressed on the intro), and Start pressed
// after waking must take the game on from it.  --wake-late
// sends [0011 Reset Exit] before the save state (loaded into the game
// already running, by its timer interrupt).
//
//   Vcore_top [--fw FILE] [--pack FILE] [--ms N] [--sleep [--wake-late]]
#include "Vcore_top.h"
#include "Vcore_top___024root.h"
#include "verilated.h"
#include "sdram_model.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static Vcore_top *top;
static Sdram sd;
static uint64_t cyc;
static const double CLK_HZ = 74.25e6;

static void dbg_poll()
{
    auto *r = top->rootp;
    if (r->core_top__DOT__soc__DOT__dbg_putc) fputc(r->core_top__DOT__soc__DOT__dbg_char, stderr);
}

// one clk_74a cycle; the SDRAM chip takes its pins half a cycle after the edge
static void tick()
{
    top->clk_74a = 1; top->clk_74b = 1;
    top->eval();
    dbg_poll();
    // half a cycle later the SDRAM's clock rises: the chip takes the pins and
    // drives read data on DQ until its next edge, which the core's pad
    // register (core_top.v) catches at the next rising edge of the system clock
    if (top->dram_cke) {
        sd.edge(cyc, top->dram_ras_n, top->dram_cas_n, top->dram_we_n, top->dram_ba, top->dram_a,
                top->dram_dqm, top->dram_dq__out, top->dram_dq__en != 0);
    }
    bool v;
    int d = sd.drive(cyc, &v);
    top->dram_dq = v ? d : 0xdead;
    top->clk_74a = 0; top->clk_74b = 0;
    top->eval();
    cyc++;
}

static void ticks(int n) { while (n--) tick(); }

static void bridge_write(uint32_t addr, uint32_t data)
{
    top->bridge_addr = addr; top->bridge_wr_data = data; top->bridge_wr = 1;
    tick();
    top->bridge_wr = 0;
    ticks(40);                          // the APF bridge: a few MB/s
}

// the bridge's reads are pipelined: the data for a strobe may come as late
// as the next strobe (the template's command handler answers from block RAM
// a cycle late), so the address is set up first
static uint32_t bridge_read(uint32_t addr)
{
    top->bridge_addr = addr;
    ticks(4);
    top->bridge_rd = 1;
    tick();
    top->bridge_rd = 0;
    ticks(8);
    return top->bridge_rd_data;
}

static void host_cmd(uint16_t cmd, uint32_t p0 = 0, uint32_t p1 = 0)
{
    bridge_write(0xF8000020, p0);
    bridge_write(0xF8000024, p1);
    bridge_write(0xF8000000, 0x434D0000u | cmd);
    ticks(200);
    uint32_t st = bridge_read(0xF8000000);
    fprintf(stderr, "tb: host command %04x -> status %08x\n", cmd, st);
}

// frames from the video port (320x200, 24-bit)
struct Grab {
    std::vector<uint32_t> cur, last;
    int last_vs = 0, frames = 0;
    void step() {
        if (top->video_vs && !last_vs) { if (cur.size() == 320 * 200) { last = cur; frames++; } cur.clear(); }
        last_vs = top->video_vs;
        if (top->video_de) cur.push_back(top->video_rgb & 0xffffff);
    }
};
static Grab grab;
static void run_ms(double ms, std::vector<std::vector<uint32_t>> *keep = nullptr)
{
    uint64_t until = cyc + (uint64_t)(ms * 1e-3 * CLK_HZ);
    int seen = grab.frames;
    while (cyc < until) {
        tick();
        grab.step();
        if (keep && grab.frames != seen) { keep->push_back(grab.last); seen = grab.frames; }
    }
}
static double same_frac(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
    if (a.size() != b.size() || a.empty()) return 0;
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i++) n += a[i] == b[i];
    return (double)n / a.size();
}
static int lit(const std::vector<uint32_t> &a)
{
    int n = 0;
    for (uint32_t p : a) n += p != 0;
    return n;
}

static std::vector<uint8_t> load(const char *path)
{
    std::vector<uint8_t> v;
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); v.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(v.data(), 1, v.size(), f) != v.size()) { perror(path); exit(1); }
    fclose(f);
    return v;
}

static void write_slot(uint32_t base, const std::vector<uint8_t> &data)
{
    for (size_t i = 0; i < data.size(); i += 4) {
        uint8_t b[4] = {0, 0, 0, 0};
        memcpy(b, &data[i], std::min<size_t>(4, data.size() - i));
        // big endian on the bridge (bridge_endian_little = 0)
        bridge_write(base + (uint32_t)i, (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]);
    }
}

// the Pocket's reads: the data for a read is taken at the next read strobe
static std::vector<uint8_t> read_stream(uint32_t base, uint32_t bytes)
{
    std::vector<uint8_t> out;
    uint32_t n = (bytes + 3) / 4;
    for (uint32_t i = 0; i <= n; i++) {
        top->bridge_addr = base + 4 * (i < n ? i : n - 1);
        // io_bridge_peripheral: the address, 4 cycles, the data taken, the strobe
        for (int k = 0; k < 4; k++) { tick(); grab.step(); }
        if (i > 0) {                    // the previous word, as the strobe comes
            uint32_t w = top->bridge_rd_data;
            out.push_back(w >> 24); out.push_back(w >> 16); out.push_back(w >> 8); out.push_back(w);
        }
        if (i == n) break;
        top->bridge_rd = 1; tick(); grab.step();
        top->bridge_rd = 0;
        for (int k = 0; k < 40; k++) { tick(); grab.step(); }
    }
    out.resize(bytes);
    return out;
}

static uint32_t host_poll(uint16_t cmd, int tries)
{
    for (int i = 0; i < tries; i++) {
        bridge_write(0xF8000020, 0);
        bridge_write(0xF8000000, 0x434D0000u | cmd);
        ticks(200);
        uint32_t st = bridge_read(0xF8000000);
        if ((st & 0xffff) >= 2) return st;
        run_ms(5);
    }
    return 0xffffffff;
}

static void power_on(void)
{
    top = new Vcore_top;
    sd = Sdram();
    grab = Grab();
    top->cont1_key = 0x10000000;
    ticks(100);
    ticks(20000);                       // the SDRAM's 200 us
}

static std::vector<uint8_t> make_save(void)
{
    // skyroads.cfg with controls 0, sound on, road 1 done 7 times
    std::vector<uint8_t> save(8 + 66, 0);
    memcpy(&save[0], "SKY1", 4);
    save[4] = 66;
    int sum = 0;
    for (int i = 1; i <= 32; i++) sum += 0 ^ i;
    save[8] = sum & 0xff; save[9] = sum >> 8;
    save[14] = 7;
    return save;
}

static int sleep_test(const std::vector<uint8_t> &fw, const std::vector<uint8_t> &pack, bool wake_late, double save_at)
{
    std::vector<uint8_t> save = make_save();
    // ---- the game runs; the Pocket goes to sleep
    power_on();
    write_slot(0x00000000, fw); write_slot(0x00800000, pack); write_slot(0x20000000, save);
    host_cmd(0x008F); host_cmd(0x0011);
    std::vector<std::vector<uint32_t>> boot;    // what a fresh start shows, frame by frame
    // Start on the intro, then the save state on the main menu: the game
    // waiting for a key there (SDL_Delay), the wait the wake must not stretch
    run_ms(save_at - 1000, &boot);
    top->cont1_key = 0x10000000 | 1u << 15;
    run_ms(100, &boot);
    top->cont1_key = 0x10000000;
    run_ms(900, &boot);
    fprintf(stderr, "tb: %.0f ms, %d frames: [00A0] save state\n", cyc / CLK_HZ * 1e3, grab.frames);
    host_cmd(0x00A0, 1);
    uint32_t st = host_poll(0x00A0, 400);
    uint32_t supported = bridge_read(0xF8000040), addr = bridge_read(0xF8000044), size = bridge_read(0xF8000048);
    fprintf(stderr, "tb: [00A0] status %08x, supported %u, blob at %08x, %u bytes\n", st, supported, addr, size);
    if ((st & 0xffff) != 2 || size == 0 || size > 0x01000000) { fprintf(stderr, "tb: FAIL (no save state)\n"); return 1; }
    std::vector<uint8_t> blob = read_stream(addr, size);
    size_t bad = 0;
    for (size_t i = 0; i + 1 < blob.size(); i += 2)
        bad += sd.mem[(0x02000000 + i) / 2] != (blob[i] | blob[i + 1] << 8);
    fprintf(stderr, "tb: read the blob over the bridge (%.0f ms): magic %c%c%c%c, %zu of %zu half words differ from the SDRAM\n",
            cyc / CLK_HZ * 1e3, blob[0], blob[1], blob[2], blob[3], bad, blob.size() / 2);
    std::vector<std::vector<uint32_t>> after;   // what the game went on to show
    run_ms(900, &after);
    fprintf(stderr, "tb: the first run went on: %zu frames, sdram errors %d\n", after.size(), sd.errors);
    int errors_a = sd.errors;
    host_cmd(0x0010);
    top->final();
    delete top;

    // ---- power off; the Pocket wakes: the core, the slots, the save state
    power_on();
    write_slot(0x00000000, fw); write_slot(0x00800000, pack); write_slot(0x20000000, save);
    host_cmd(0x008F);
    uint64_t reset_exit = cyc;
    if (wake_late) { host_cmd(0x0011); reset_exit = cyc; run_ms(300); }
    write_slot(addr, blob);
    host_cmd(0x00A4, 1);
    st = host_poll(0x00A4, 50);
    fprintf(stderr, "tb: [00A4] status %08x\n", st);
    if (!wake_late) { host_cmd(0x0011); reset_exit = cyc; }
    double t0 = cyc / CLK_HZ * 1e3;
    std::vector<std::vector<uint32_t>> woke;
    run_ms(400, &woke);
    // the last frame after waking against everything the first run showed
    double best = 0; size_t at = 0;
    for (size_t i = 0; i < after.size(); i++) {
        double f = same_frac(woke.back(), after[i]);
        if (f > best) { best = f; at = i; }
    }
    int lit_w = lit(woke.back());
    // a fresh start, as long after Reset Exit: what the screen would be if
    // the save state had not been taken
    size_t k = (size_t)((cyc - reset_exit) / CLK_HZ * 60.0) - 1;     // 60 frames a second
    double fresh = k < boot.size() ? same_frac(woke.back(), boot[k]) : 0;
    size_t changes = 0;
    for (size_t i = 1; i < after.size(); i++) changes += same_frac(after[i], after[i - 1]) < 1.0;
    fprintf(stderr, "tb: the first run's continuation changed %zu times in %zu frames; a fresh start at that time: %.2f%% identical\n",
            changes, after.size(), fresh * 100);
    fprintf(stderr, "tb: %.0f ms after waking: %d lit pixels; best match: frame %zu of the first run's continuation, %.2f%% of pixels identical\n",
            cyc / CLK_HZ * 1e3 - t0, lit_w, at, best * 100);
    // the controls after waking: Start (Enter) must take the game on from
    // the screen it woke on, which without it stays as the first run went
    // on to show (a wait in progress when the state was saved must not
    // outlast the wake: the menus wait for a key in SDL_Delay)
    top->cont1_key = 0x10000000 | 1u << 15;
    run_ms(100);
    top->cont1_key = 0x10000000;
    std::vector<std::vector<uint32_t>> pressed;
    run_ms(600, &pressed);
    double still = 0;
    for (size_t i = 0; i < after.size(); i++) still = std::max(still, same_frac(pressed.back(), after[i]));
    fprintf(stderr, "tb: Start pressed after waking: %.2f%% like the screen it woke on\n", still * 100);
    bool ok = (st & 0xffff) == 2 && bad == 0 && best >= 0.98 && fresh < 0.9 && lit_w > 1000 &&
              still < 0.9 && errors_a == 0 && sd.errors == 0;
    fprintf(stderr, "tb: sleep %s\n", ok ? "PASS" : "FAIL");
    top->final();
    delete top;
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *fw_path = "../build/skyroads_fw_sim.bin", *pack_path = "../build/skyroads_data.bin";
    double run_for = 1500;
    bool sleep = false, wake_late = false;
    double save_at = 3000;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--fw" && i + 1 < argc) fw_path = argv[++i];
        else if (a == "--pack" && i + 1 < argc) pack_path = argv[++i];
        else if (a == "--ms" && i + 1 < argc) run_for = atof(argv[++i]);
        else if (a == "--sleep") sleep = true;
        else if (a == "--wake-late") wake_late = true;
        else if (a == "--save-at" && i + 1 < argc) save_at = atof(argv[++i]);
    }
    if (sleep) {
        Verilated::commandArgs(argc, argv);
        return sleep_test(load(fw_path), load(pack_path), wake_late, save_at);
    }
    Verilated::commandArgs(argc, argv);
    top = new Vcore_top;
    top->cont1_key = 0x10000000;        // type: Pocket buttons, none pressed
    ticks(100);

    // a save file: skyroads.cfg with controls 0, sound on, road 1 done 7 times
    std::vector<uint8_t> save(8 + 66, 0);
    memcpy(&save[0], "SKY1", 4);
    save[4] = 66;
    {
        int sum = 0;
        for (int i = 1; i <= 32; i++) sum += 0 ^ i;
        save[8] = sum & 0xff; save[9] = sum >> 8;
        save[14] = 7;
    }
    std::vector<uint8_t> fw = load(fw_path), pack = load(pack_path);
    fprintf(stderr, "tb: writing %zu + %zu + %zu bytes over the bridge\n", fw.size(), pack.size(), save.size());
    // the SDRAM needs 200 us to initialise; the Pocket is slower than that anyway
    ticks(20000);
    write_slot(0x00000000, fw);
    write_slot(0x00800000, pack);
    write_slot(0x20000000, save);
    fprintf(stderr, "tb: slots written at %.1f ms\n", cyc / CLK_HZ * 1e3);
    host_cmd(0x008F);                   // data slot access all complete
    host_cmd(0x0011);                   // reset exit: run
    uint32_t dt_id = bridge_read(0xF8002010), dt_size = bridge_read(0xF8002014);
    fprintf(stderr, "tb: data slot table entry 2: id %u, size %u\n", dt_id, dt_size);

    uint64_t until = cyc + (uint64_t)(run_for * 1e-3 * CLK_HZ);
    int frames = 0, last_vs = 0, lit = 0, lit_frame = 0;
    while (cyc < until) {
        tick();
        if (top->video_vs && !last_vs) { frames++; lit_frame = lit; lit = 0; }
        last_vs = top->video_vs;
        if (top->video_de && (top->video_rgb & 0xffffff)) lit++;
        {   /* trace the first line reads after the CPU starts */
            static int traced;
            auto *r = top->rootp;
            if (traced < 6 && r->core_top__DOT__soc__DOT__m_ack && !r->core_top__DOT__soc__DOT__m_we) {
                uint32_t a = r->core_top__DOT__soc__DOT__m_addr << 4;
                uint32_t got = r->core_top__DOT__soc__DOT__m_rdata[0];
                uint32_t want = a + 4 <= fw.size() ? fw[a] | fw[a + 1] << 8 | fw[a + 2] << 16 | fw[a + 3] << 24 : 0;
                fprintf(stderr, "tb: line read %06x: %08x (image %08x)\n", a, got, want);
                traced++;
            }
        }
        if (top->rootp->core_top__DOT__soc__DOT__sim_exit) {
            fprintf(stderr, "tb: firmware exit %u\n", top->rootp->core_top__DOT__soc__DOT__sim_code);
            break;
        }
    }
    uint32_t vram_base = top->rootp->core_top__DOT__soc__DOT__vram_base;
    {   /* diagnostics: run control and what the loader put in the SDRAM */
        auto *r = top->rootp;
        fprintf(stderr, "tb: reset_n_sys %d allcomplete_sys %d ldq_empty %d sdram_ready %d cpu_rst_n %d\n",
                (int)r->core_top__DOT__reset_n_sys, (int)r->core_top__DOT__allcomplete_sys,
                (int)r->core_top__DOT__ldq_empty, (int)r->core_top__DOT__sdram_ready,
                (int)r->core_top__DOT__soc__DOT__cpu_rst_n);
        size_t bad = 0, first = (size_t)-1;
        for (size_t i = 0; i + 1 < fw.size(); i += 2) {
            uint16_t want = fw[i] | fw[i + 1] << 8;
            if (sd.mem[i / 2] != want) { if (first == (size_t)-1) first = i; bad++; }
        }
        fprintf(stderr, "tb: firmware in SDRAM: %zu of %zu half words differ (first at byte %zx: %04x, want %02x%02x)\n",
                bad, fw.size() / 2, first, first == (size_t)-1 ? 0 : sd.mem[first / 2],
                first == (size_t)-1 ? 0 : fw[first + 1], first == (size_t)-1 ? 0 : fw[first]);
    }
    fprintf(stderr, "tb: ran %.0f ms after reset exit, %d frames, sdram errors %d\n", run_for, frames, sd.errors);
    fprintf(stderr, "tb: VRAM base %08x (set by the firmware), last frame %d lit pixels\n", vram_base, lit_frame);

    host_cmd(0x0010);                   // reset enter, as on exit
    // the save file, read as the Pocket reads it (all 1 KiB, the datatable's size)
    std::vector<uint8_t> back = read_stream(0x20000000, 1024);
    bool same = true;
    for (size_t i = 0; i < save.size(); i++)
        if (back[i] != save[i]) { same = false; fprintf(stderr, "tb: save byte %zu: %02x, wrote %02x\n", i, back[i], save[i]); }
    fprintf(stderr, "tb: save read back over the bridge %s\n", same ? "identical" : "DIFFERENT");
    bool ok = same && dt_size == 1024 && sd.errors == 0 && frames > 0 &&
              vram_base != 0xffff0000u && lit_frame > 1000;
    fprintf(stderr, "tb: %s\n", ok ? "PASS" : "FAIL");
    top->final();
    delete top;
    return ok ? 0 : 1;
}
