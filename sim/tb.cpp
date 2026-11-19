// Verilator testbench for the SkyRoads SoC (rtl/sky_soc.v).
//
// Models the SDRAM chip at its pins (commands, CAS latency 2, bursts of 8,
// DQM, auto precharge, with timing checks), clocked like on the Pocket by
// the inverted system clock: it takes the pins half a cycle after each
// rising edge, and what it drives then is caught by the pad register at
// the next rising edge (core_top.v, sdram_ctrl.v RD_LAT), loads the firmware and the data
// pack through the SoC's loader port as the APF bridge would, releases the
// CPU, and records what comes out: frames (PPM) from the video port,
// audio (raw s16le mono 48 kHz) and the firmware's debug output.
//
//   Vsky_soc [options]
//     --fw FILE        firmware (default ../build/skyroads_fw_sim.bin)
//     --pack FILE      data pack (default ../build/skyroads_data.bin)
//     --frames DIR     write frames here
//     --every N        every Nth frame (default 30)
//     --ms N           stop after N ms of simulated time
//     --keys ms:hex,.. set the buttons (cont1_key) at simulated times
//     --settings ms:hex,..  set the Core Settings (interact.json) at times
//     --wav FILE       record the audio
// Exits with the firmware's exit code (the sim build ends after the demo).
#include "Vsky_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

static const double SYS_HZ = 49.152e6;      // sky_soc's SYS_HZ
static const double VID_HZ = 12.288e6;

#include "sdram_model.h"

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

int main(int argc, char **argv)
{
    const char *fw_path = "../build/skyroads_fw_sim.bin", *pack_path = "../build/skyroads_data.bin";
    const char *frames = nullptr, *keys = nullptr, *wav_path = nullptr, *settings = nullptr;
    int every = 30;
    double stop_ms = 1e12;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : (char *)""; };
        if (a == "--fw") fw_path = next();
        else if (a == "--pack") pack_path = next();
        else if (a == "--frames") frames = next();
        else if (a == "--every") every = atoi(next());
        else if (a == "--ms") stop_ms = atof(next());
        else if (a == "--keys") keys = next();
        else if (a == "--settings") settings = next();
        else if (a == "--wav") wav_path = next();
    }
    std::vector<uint8_t> fw = load(fw_path), pack = load(pack_path);
    // what the loader writes: (byte address, little endian word)
    std::vector<std::pair<uint32_t, uint32_t>> words;
    for (size_t i = 0; i < fw.size(); i += 4) {
        uint32_t w = 0; memcpy(&w, &fw[i], std::min<size_t>(4, fw.size() - i)); words.push_back({(uint32_t)i, w});
    }
    for (size_t i = 0; i < pack.size(); i += 4) {
        uint32_t w = 0; memcpy(&w, &pack[i], std::min<size_t>(4, pack.size() - i)); words.push_back({0x800000u + (uint32_t)i, w});
    }

    Verilated::commandArgs(argc, argv);
    Vsky_soc *top = new Vsky_soc;
    Sdram sd;
    FILE *wav = wav_path ? fopen(wav_path, "wb") : nullptr;

    top->clk = 0; top->clk_vid = 0; top->clk_b = 0;
    top->rst_n = 0; top->vid_rst_n = 0; top->cpu_run = 0;
    top->keys = 0; top->ld_valid = 0; top->save_we = 0; top->save_addr = 0;
    top->eval();

    std::vector<uint8_t> frame(320 * 200 * 3);
    int fx = 0, fy = 0, nframes = 0, last_de = 0;
    uint64_t cyc = 0;
    double vid_phase = 0;
    int dq_prev = 0;                    // the chip's drive in the last cycle
    size_t ld_next = 0;
    bool running = false;
    uint64_t run_at = 0;
    uint32_t cur_keys = 0;
    const char *key_next = keys, *set_next = settings;
    auto wall0 = std::chrono::steady_clock::now();
    double audio_phase = 0;
    int exit_code = -1;

    for (;; cyc++) {
        double ms = cyc / (SYS_HZ / 1000);
        if (ms > stop_ms) break;
        if (cyc == 20) { top->rst_n = 1; top->vid_rst_n = 1; }

        // loader: one word at a time, as from the APF bridge
        if (top->rst_n && top->sdram_ready && ld_next < words.size()) {
            top->ld_valid = 1;
            top->ld_addr = words[ld_next].first >> 2;
            top->ld_data = words[ld_next].second;
        } else top->ld_valid = 0;
        if (!running && ld_next == words.size() && top->sdram_ready) {
            running = true; run_at = cyc;
            top->cpu_run = 1;
            fprintf(stderr, "tb: loaded %zu words by %.1f ms, CPU running\n", words.size(), ms);
        }
        if (running) {
            while (key_next && *key_next) {
                char *end;
                double at = strtod(key_next, &end);
                if (ms < at || *end != ':') break;
                cur_keys = (uint32_t)strtoul(end + 1, &end, 16);
                key_next = *end == ',' ? end + 1 : nullptr;
                fprintf(stderr, "tb: %.0f ms keys %x\n", ms, cur_keys);
            }
            top->keys = cur_keys;
        }
        while (set_next && *set_next) {
            char *end;
            double at = strtod(set_next, &end);
            if (ms < at || *end != ':') break;
            top->settings = (uint32_t)strtoul(end + 1, &end, 16);
            set_next = *end == ',' ? end + 1 : nullptr;
            fprintf(stderr, "tb: %.0f ms settings %x\n", ms, (unsigned)top->settings);
        }

        // rising edge of the system clock (and the video clock in between)
        top->clk = 1;
        top->eval();
        if (top->ld_ready) ld_next++;
        if (top->dbg_putc) fputc(top->dbg_char, stderr);
        if (top->sim_exit) { exit_code = top->sim_code; break; }
        // the pad register caught what the chip drove since its last edge
        top->sd_dq_in = dq_prev;
        // half a cycle later the SDRAM's clock rises: the chip takes the pins
        // just registered, and drives read data until its next edge
        if (top->rst_n) sd.edge(cyc, top->sd_ras_n, top->sd_cas_n, top->sd_we_n, top->sd_ba, top->sd_a,
                                top->sd_dqm, top->sd_dq_out, top->sd_dq_oe);
        bool v;
        int drive = sd.drive(cyc, &v);
        dq_prev = v ? drive : 0xdead;
        top->clk = 0;
        top->eval();

        vid_phase += VID_HZ / SYS_HZ;
        while (vid_phase >= 0.5) {      // a video clock edge
            vid_phase -= 0.5;
            top->clk_vid = !top->clk_vid;
            top->eval();
            if (top->clk_vid) {
                if (top->vid_vs) {
                    if (frames && fy > 0 && nframes % every == 0) {
                        char path[512];
                        snprintf(path, sizeof path, "%s/frame_%05d.ppm", frames, nframes);
                        FILE *f = fopen(path, "wb");
                        if (f) { fprintf(f, "P6 320 200 255\n"); fwrite(frame.data(), 1, frame.size(), f); fclose(f); }
                    }
                    if (fy > 0) nframes++;
                    fy = 0; fx = 0;
                }
                if (top->vid_de) {
                    if (fx < 320 && fy < 200) {
                        uint8_t *p = &frame[(fy * 320 + fx) * 3];
                        p[0] = top->vid_rgb >> 16; p[1] = top->vid_rgb >> 8; p[2] = top->vid_rgb;
                    }
                    fx++;
                } else if (last_de) { fx = 0; fy++; }
                last_de = top->vid_de;
            }
        }

        if (wav) {
            audio_phase += 48000.0 / SYS_HZ;
            if (audio_phase >= 1) { audio_phase -= 1; int16_t s = (int16_t)top->sample; fwrite(&s, 2, 1, wav); }
        }

        if (cyc % 50000000 == 0 && cyc) {
            double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
            fprintf(stderr, "tb: %.0f ms simulated, %d frames, %.2f MHz, sdram refreshes %llu (max gap %llu cycles)\n",
                    ms, nframes, cyc / wall / 1e6, (unsigned long long)sd.refreshes,
                    (unsigned long long)sd.max_ref_gap);
        }
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    fprintf(stderr, "tb: stopped at %.1f ms (CPU ran %.1f ms), %d frames, %.2f MHz, exit code %d, sdram errors %d\n",
            cyc / (SYS_HZ / 1000), (cyc - run_at) / (SYS_HZ / 1000), nframes, cyc / wall / 1e6,
            exit_code, sd.errors);
    if (wav) fclose(wav);
    top->final();
    delete top;
    return exit_code < 0 ? 3 : exit_code;
}
