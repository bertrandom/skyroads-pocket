// SDRAM chip model shared by the testbenches (tb.cpp, tb_core.cpp): the
// Pocket's 64 MiB x16 SDRAM at its pins, clocked by the inverted system
// clock (see tb.cpp for how it is driven).
#pragma once
#include <cstdio>
#include <cstdint>
#include <vector>

// ---- SDRAM chip model -------------------------------------------------
struct Sdram {
    std::vector<uint16_t> mem = std::vector<uint16_t>(32u << 20);   // 64 MiB
    int  row_open[4] = {-1, -1, -1, -1};
    uint64_t act_at[4] = {0}, pre_at[4] = {0};
    bool ap_pending[4] = {false};
    uint64_t ap_done[4] = {0};
    int  mode_cl = 0, mode_bl = 0;
    bool init_done = false;
    // read burst
    int  rd_bank = -1, rd_beats = 0;
    uint32_t rd_addr = 0;
    uint64_t rd_start = 0;
    // write burst
    int  wr_beats = 0, wr_bank = 0;
    uint32_t wr_addr = 0;
    uint64_t refreshes = 0, last_ref = 0, max_ref_gap = 0;
    int errors = 0;
    void err(uint64_t cyc, const char *m) {
        if (errors++ < 20) fprintf(stderr, "sdram @%llu: %s\n", (unsigned long long)cyc, m);
    }
    static uint32_t haddr(int bank, int row, int col) {
        return ((uint32_t)bank << 23) | ((uint32_t)row << 10) | (uint32_t)col;
    }
    // cycle timing at 50 MHz: tRCD 1 (18 ns -> 1), tRP 1, tRFC 4
    void edge(uint64_t cyc, int ras, int cas, int we, int ba, int a, int dqm, int dq, int oe) {
        // write burst data (beats after the first)
        if (wr_beats > 0) {
            if (!oe) err(cyc, "write burst without data");
            uint32_t ad = wr_addr;
            if (!(dqm & 1)) mem[ad] = (mem[ad] & 0xff00) | (dq & 0x00ff);
            if (!(dqm & 2)) mem[ad] = (mem[ad] & 0x00ff) | (dq & 0xff00);
            wr_addr = (wr_addr & ~7u) | ((wr_addr + 1) & 7u);
            if (--wr_beats == 0) { ap_pending[wr_bank] = true; ap_done[wr_bank] = cyc + 2 + 1; }
        }
        for (int b = 0; b < 4; b++)
            if (ap_pending[b] && cyc >= ap_done[b]) { ap_pending[b] = false; row_open[b] = -1; pre_at[b] = cyc; }
        int c = ras << 2 | cas << 1 | we;
        switch (c) {
            case 7: break;                                  // NOP
            case 3: {                                       // ACTIVE
                if (row_open[ba] >= 0 || ap_pending[ba]) err(cyc, "ACT to an open bank");
                if (cyc - pre_at[ba] < 1) err(cyc, "tRP");
                row_open[ba] = a & 0x1fff; act_at[ba] = cyc;
                break;
            }
            case 5: case 4: {                               // READ / WRITE
                if (!init_done) err(cyc, "access before init");
                if (row_open[ba] < 0) { err(cyc, "access to a closed bank"); break; }
                if (cyc - act_at[ba] < 1) err(cyc, "tRCD");
                if (!(a & 0x400)) err(cyc, "no auto precharge");
                int col = a & 0x3ff;
                if (col & 7) err(cyc, "unaligned burst");
                uint32_t ad = haddr(ba, row_open[ba], col);
                if (c == 5) {
                    rd_bank = ba; rd_addr = ad; rd_beats = 8; rd_start = cyc + mode_cl;
                    ap_pending[ba] = true; ap_done[ba] = cyc + 8 + 1;
                } else {
                    if (!oe) err(cyc, "WRITE without data");
                    if (!(dqm & 1)) mem[ad] = (mem[ad] & 0xff00) | (dq & 0x00ff);
                    if (!(dqm & 2)) mem[ad] = (mem[ad] & 0x00ff) | (dq & 0xff00);
                    wr_addr = (ad & ~7u) | ((ad + 1) & 7u); wr_beats = 7; wr_bank = ba;
                }
                break;
            }
            case 2:                                         // PRECHARGE
                if (a & 0x400) for (int b = 0; b < 4; b++) { row_open[b] = -1; pre_at[b] = cyc; }
                else { row_open[ba] = -1; pre_at[ba] = cyc; }
                break;
            case 1:                                         // REFRESH
                for (int b = 0; b < 4; b++) if (row_open[b] >= 0 || ap_pending[b]) err(cyc, "REFRESH with an open bank");
                if (init_done) {
                    if (refreshes && cyc - last_ref > max_ref_gap) max_ref_gap = cyc - last_ref;
                    refreshes++;
                }
                last_ref = cyc;
                break;
            case 0:                                         // MODE REGISTER SET
                mode_cl = (a >> 4) & 7; mode_bl = a & 7;
                if (mode_bl != 3) err(cyc, "burst length is not 8");
                init_done = true;
                break;
            default: err(cyc, "unexpected command");
        }
    }
    // what the chip drives on DQ in the cycle after edge `cyc`
    int drive(uint64_t cyc, bool *valid) {
        *valid = false;
        if (rd_beats > 0 && cyc >= rd_start) {
            int v = mem[rd_addr];
            rd_addr = (rd_addr & ~7u) | ((rd_addr + 1) & 7u);
            rd_beats--;
            *valid = true;
            return v;
        }
        return 0;
    }
};

