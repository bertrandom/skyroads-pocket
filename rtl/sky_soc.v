// The SkyRoads SoC: a Hazard3 RISC-V CPU (RV32IMAC + Zba/Zbb/Zbs) running
// the port's game code from SDRAM through two caches, with a video RAM, a
// palette, an OPL2, a sample queue, a PC speaker and a timer.  See
// ../firmware/pocket_hw.h for the address map and the registers.
//
// Clock domains: clk (system: CPU, caches, SDRAM, audio), clk_vid (video
// scan-out), clk_b (the APF bridge's side of the save RAM).  The bridge
// loader's writes arrive already in the system domain (core_top.v).
module sky_soc #(
    parameter SYS_HZ  = 49152000,
    parameter SD_CL   = 2,
    parameter SD_RD_LAT = SD_CL + 1,
    parameter IC_SETS_LOG2 = 10,        // instruction cache: 1024 lines, 16 KiB
    parameter DC_SETS_LOG2 = 11         // data cache: 2048 lines, 32 KiB
) (
    input  wire         clk,
    input  wire         rst_n,          // system: SDRAM, loader, audio
    input  wire         cpu_run,        // CPU and caches out of reset

    input  wire [31:0]  keys,           // cont1_key, synchronised
    input  wire [3:0]   dbg_status,     // core_top's run control, for the status screen
    input  wire [31:0]  settings,       // Core Settings, from the bridge (quasi-static)

    // the bridge loader (system clock): one 32-bit word per request
    input  wire         ld_valid,
    input  wire [25:2]  ld_addr,
    input  wire [31:0]  ld_data,        // little endian
    output wire         ld_ready,
    output wire         sdram_ready,

    // save states (system clock; core_top.v crosses them from the bridge).
    // The staging area is the last 32 MiB of the SDRAM (CPU 0x12000000):
    // the firmware writes a blob there and the bridge reads it, or the
    // bridge writes one there (through the loader) and the firmware reads it
    input  wire         ss_save_req,    // pulse: the Pocket asks for a save state
    input  wire         ss_load_req,    // pulse: a blob is in the staging area
    output reg          ss_save_done,   // pulse: the blob is ready, ss_size bytes
    output reg          ss_save_fail,   // pulse
    output reg  [31:0]  ss_size,
    input  wire         ss_rd_req,      // pulse: the bridge reads staging word ss_rd_addr
    input  wire [22:0]  ss_rd_addr,
    output reg  [31:0]  ss_rd_data,     // by the bridge's next read

    // save RAM, bridge side
    input  wire         clk_b,
    input  wire [7:0]   save_addr,
    input  wire         save_we,
    input  wire [31:0]  save_wdata,
    output wire [31:0]  save_q,

    // video
    input  wire         clk_vid,
    input  wire         vid_rst_n,
    output wire [23:0]  vid_rgb,
    output wire         vid_de,
    output wire         vid_hs,
    output wire         vid_vs,

    // audio
    output wire signed [15:0] sample,

    // SDRAM
    output wire         sd_cke,
    output wire [1:0]   sd_ba,
    output wire [12:0]  sd_a,
    output wire         sd_ras_n,
    output wire         sd_cas_n,
    output wire         sd_we_n,
    output wire [1:0]   sd_dqm,
    output wire [15:0]  sd_dq_out,
    output wire         sd_dq_oe,
    input  wire [15:0]  sd_dq_in,

    // simulation hooks
    output reg          dbg_putc,
    output reg  [7:0]   dbg_char,
    output reg          sim_exit,
    output reg  [7:0]   sim_code
);
    // CPU and caches: held in reset until the game is loaded
    reg [1:0] cpu_rst_s;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) cpu_rst_s <= 2'b00;
        else cpu_rst_s <= {cpu_rst_s[0], cpu_run};
    wire cpu_rst_n = cpu_rst_s[1];

    // ---- CPU ----------------------------------------------------------
    wire [31:0] i_haddr, d_haddr, d_hwdata, i_hwdata;
    wire [1:0]  i_htrans, d_htrans;
    wire [2:0]  i_hsize, d_hsize, i_hburst, d_hburst;
    wire        i_hwrite, d_hwrite;
    wire [31:0] i_hrdata;
    reg  [31:0] d_hrdata;
    wire        i_hready;
    reg         d_hready;
    reg         timer_irq;
    wire        pwrup_req, unblock_out;

    hazard3_cpu_2port #(
        .RESET_VECTOR     (32'h10000000),
        .MTVEC_INIT       (32'h10000000),
        .EXTENSION_A      (1),
        .EXTENSION_C      (1),
        .EXTENSION_M      (1),
        .EXTENSION_ZBA    (1),
        .EXTENSION_ZBB    (1),
        .EXTENSION_ZBS    (1),
        .CSR_COUNTER      (1),
        .MUL_FAST         (1),
        .MULH_FAST        (1),
        .BRANCH_PREDICTOR (1),
        .NUM_IRQS         (1)
    ) u_cpu (
        .clk(clk), .clk_always_on(clk), .rst_n(cpu_rst_n),
        .pwrup_req(pwrup_req), .pwrup_ack(pwrup_req), .clk_en(),
        .unblock_out(unblock_out), .unblock_in(unblock_out),
        .i_haddr(i_haddr), .i_hwrite(i_hwrite), .i_htrans(i_htrans), .i_hsize(i_hsize),
        .i_hburst(i_hburst), .i_hprot(), .i_hmastlock(), .i_hmaster(),
        .i_hready(i_hready), .i_hresp(1'b0), .i_hwdata(i_hwdata), .i_hrdata(i_hrdata),
        .d_haddr(d_haddr), .d_hwrite(d_hwrite), .d_htrans(d_htrans), .d_hsize(d_hsize),
        .d_hburst(d_hburst), .d_hprot(), .d_hmastlock(), .d_hmaster(), .d_hexcl(),
        .d_hready(d_hready), .d_hresp(1'b0), .d_hexokay(1'b1),
        .d_hwdata(d_hwdata), .d_hrdata(d_hrdata),
        .fence_i_vld(), .fence_d_vld(), .fence_rdy(1'b1),
        .dbg_req_halt(1'b0), .dbg_req_halt_on_reset(1'b0), .dbg_req_resume(1'b0),
        .dbg_halted(), .dbg_running(),
        .dbg_data0_rdata(32'h0), .dbg_data0_wdata(), .dbg_data0_wen(),
        .dbg_instr_data(32'h0), .dbg_instr_data_vld(1'b0), .dbg_instr_data_rdy(),
        .dbg_instr_caught_exception(), .dbg_instr_caught_ebreak(),
        .dbg_sbus_addr(32'h0), .dbg_sbus_write(1'b0), .dbg_sbus_size(2'h0), .dbg_sbus_vld(1'b0),
        .dbg_sbus_rdy(), .dbg_sbus_err(), .dbg_sbus_wdata(32'h0), .dbg_sbus_rdata(),
        .mhartid_val(32'h0), .eco_version(4'h0),
        .irq(1'b0), .soft_irq(1'b0), .timer_irq(timer_irq)
    );

    // ---- instruction side: everything through the I-cache --------------
    wire         ic_req, ic_we, ic_ack;
    wire [25:4]  ic_addr;
    wire [127:0] ic_wdata, line_rdata;
    ahb_cache #(.SETS_LOG2(IC_SETS_LOG2), .WRITABLE(0)) u_icache (
        .clk(clk), .rst_n(cpu_rst_n),
        .sel(i_htrans[1]), .haddr(i_haddr), .hwrite(1'b0), .hsize(i_hsize), .hwdata(32'h0),
        .hready_in(i_hready), .hready_out(i_hready), .hrdata(i_hrdata),
        .mem_req(ic_req), .mem_we(ic_we), .mem_addr(ic_addr), .mem_wdata(ic_wdata),
        .mem_ack(ic_ack), .mem_rdata(line_rdata));

    // ---- data side: decode ----------------------------------------------
    reg  [31:0] vram_base;
    wire [31:0] vram_off = d_haddr - vram_base;
    wire d_act     = d_htrans[1];
    wire sel_vram  = d_act && vram_off[31:16] == 16'h0;
    wire sel_cache = d_act && !sel_vram && d_haddr[31:28] == 4'h1;
    wire sel_save  = d_act && d_haddr[31:28] == 4'h2;
    wire sel_regs  = d_act && d_haddr[31:28] == 4'h4;
    localparam DS_NONE = 3'd0, DS_CACHE = 3'd1, DS_VRAM = 3'd2, DS_SAVE = 3'd3, DS_REGS = 3'd4;
    reg [2:0] dsel;
    always @(posedge clk or negedge cpu_rst_n)
        if (!cpu_rst_n) dsel <= DS_NONE;
        else if (d_hready)
            dsel <= sel_vram ? DS_VRAM : sel_cache ? DS_CACHE : sel_save ? DS_SAVE :
                    sel_regs ? DS_REGS : DS_NONE;

    wire         dc_hready, vram_hready, save_hready;
    wire [31:0]  dc_hrdata, vram_hrdata, save_hrdata;
    reg  [31:0]  regs_hrdata;
    wire         dc_req, dc_we, dc_ack;
    wire [25:4]  dc_addr;
    wire [127:0] dc_wdata;
    ahb_cache #(.SETS_LOG2(DC_SETS_LOG2), .WRITABLE(1)) u_dcache (
        .clk(clk), .rst_n(cpu_rst_n),
        .sel(sel_cache), .haddr(d_haddr), .hwrite(d_hwrite), .hsize(d_hsize), .hwdata(d_hwdata),
        .hready_in(d_hready), .hready_out(dc_hready), .hrdata(dc_hrdata),
        .mem_req(dc_req), .mem_we(dc_we), .mem_addr(dc_addr), .mem_wdata(dc_wdata),
        .mem_ack(dc_ack), .mem_rdata(line_rdata));

    always @(*) begin
        d_hready = dsel == DS_CACHE ? dc_hready : dsel == DS_VRAM ? vram_hready :
                   dsel == DS_SAVE ? save_hready : 1'b1;
        case (dsel)
            DS_CACHE: d_hrdata = dc_hrdata;
            DS_VRAM:  d_hrdata = vram_hrdata;
            DS_SAVE:  d_hrdata = save_hrdata;
            DS_REGS:  d_hrdata = regs_hrdata;
            default:  d_hrdata = 32'h0;
        endcase
    end

    // ---- VRAM and save RAM ----------------------------------------------
    wire [13:0] vid_vram_addr;
    wire [31:0] vid_vram_q;
    ahb_dpram #(.AW(14), .B_WRITE(0)) u_vram (
        .clk(clk), .rst_n(cpu_rst_n), .sel(sel_vram), .word(vram_off[15:2]), .lane(d_haddr[1:0]),
        .hwrite(d_hwrite), .hsize(d_hsize), .hwdata(d_hwdata), .hready_in(d_hready),
        .hready_out(vram_hready), .hrdata(vram_hrdata),
        .clk_b(clk_vid), .b_addr(vid_vram_addr), .b_we(1'b0), .b_wdata(32'h0), .b_q(vid_vram_q));

    ahb_dpram #(.AW(8)) u_save (
        .clk(clk), .rst_n(cpu_rst_n), .sel(sel_save), .word(d_haddr[9:2]), .lane(d_haddr[1:0]),
        .hwrite(d_hwrite), .hsize(d_hsize), .hwdata(d_hwdata), .hready_in(d_hready),
        .hready_out(save_hready), .hrdata(save_hrdata),
        .clk_b(clk_b), .b_addr(save_addr), .b_we(save_we), .b_wdata(save_wdata), .b_q(save_q));

    // ---- registers ------------------------------------------------------
    reg         rg_w;
    reg [11:0]  rg_addr;
    always @(posedge clk or negedge cpu_rst_n)
        if (!cpu_rst_n) rg_w <= 1'b0;
        else if (d_hready) begin rg_w <= sel_regs && d_hwrite; rg_addr <= d_haddr[11:0]; end
    wire rg_rd_lo = dsel == DS_REGS && !rg_w && rg_addr == 12'h008 && d_hready;

    reg [63:0] cycles;
    reg [31:0] cycles_hi;
    reg [31:0] timer_period, timer_cnt;
    localparam RETRACE_PERIOD = SYS_HZ / 70086 * 1000 + (SYS_HZ % 70086) * 1000 / 70086;
    reg [31:0] retrace_cnt, retrace;
    reg [23:0] pcm_period, spk_half;
    reg        spk_on;
    reg [8:0]  volume;

    wire       wr = rg_w;
    wire [31:0] wd = d_hwdata;
    wire        opl_wr   = wr && rg_addr == 12'h030;
    wire        pcm_wr   = wr && rg_addr == 12'h040;
    wire        pcm_flush = wr && rg_addr == 12'h04c;
    wire        pal_we   = wr && rg_addr[11:10] == 2'b01;
    wire [9:0]  opl_free;
    wire [11:0] pcm_free;

    always @(posedge clk or negedge cpu_rst_n) begin
        if (!cpu_rst_n) begin
            cycles <= 0; timer_period <= 0; timer_cnt <= 0; timer_irq <= 1'b0;
            retrace_cnt <= 0; retrace <= 0; vram_base <= 32'hffff0000;
            pcm_period <= 24'd4535; spk_half <= 0; spk_on <= 1'b0; volume <= 9'd128;
            dbg_putc <= 1'b0; sim_exit <= 1'b0; dbg_char <= 0; sim_code <= 0;
        end else begin
            cycles <= cycles + 1'b1;
            if (rg_rd_lo) cycles_hi <= cycles[63:32];
            if (timer_period != 0) begin
                if (timer_cnt >= timer_period - 1) begin timer_cnt <= 0; timer_irq <= 1'b1; end
                else timer_cnt <= timer_cnt + 1'b1;
            end
            if (retrace_cnt >= RETRACE_PERIOD - 1) begin retrace_cnt <= 0; retrace <= retrace + 1'b1; end
            else retrace_cnt <= retrace_cnt + 1'b1;
            dbg_putc <= 1'b0;
            if (wr) case (rg_addr)
                12'h010: begin timer_period <= wd; timer_cnt <= 0; end
                12'h014: timer_irq <= 1'b0;
                12'h020: vram_base <= wd;
                12'h024: begin dbg_putc <= 1'b1; dbg_char <= wd[7:0]; end
                12'h028: begin sim_exit <= 1'b1; sim_code <= wd[7:0]; end
                12'h048: pcm_period <= wd[23:0];
                12'h050: spk_half <= wd[23:0];
                12'h054: spk_on <= wd[0];
                12'h058: volume <= wd[8:0];
                default: ;
            endcase
        end
    end

    reg [31:0] settings_s;
    always @(posedge clk) settings_s <= settings;

    // save state requests: kept across the CPU's reset (a load can be
    // requested before the CPU runs); the firmware answers through SS_CTRL
    reg ss_save_pend, ss_load_pend;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ss_save_pend <= 1'b0; ss_load_pend <= 1'b0;
            ss_save_done <= 1'b0; ss_save_fail <= 1'b0; ss_size <= 0;
        end else begin
            ss_save_done <= 1'b0; ss_save_fail <= 1'b0;
            if (wr && rg_addr == 12'h064) ss_size <= wd;
            if (wr && rg_addr == 12'h068) begin
                if (wd[0]) begin ss_save_pend <= 1'b0; ss_save_done <= 1'b1; end
                if (wd[1]) begin ss_save_pend <= 1'b0; ss_save_fail <= 1'b1; end
                if (wd[2]) ss_load_pend <= 1'b0;
            end
            if (ss_save_req) begin
                if (cpu_rst_n) ss_save_pend <= 1'b1;
                else ss_save_fail <= 1'b1;          // nothing running to save
            end
            if (ss_load_req) ss_load_pend <= 1'b1;
        end
    end

    always @(*) begin
        case (rg_addr)
            12'h000: regs_hrdata = 32'h31594b53;            // 'SKY1'
            12'h004: regs_hrdata = SYS_HZ;
            12'h008: regs_hrdata = cycles[31:0];
            12'h00c: regs_hrdata = cycles_hi;
            12'h010: regs_hrdata = timer_period;
            12'h018: regs_hrdata = retrace;
            12'h01c: regs_hrdata = keys;
            12'h020: regs_hrdata = vram_base;
            12'h034: regs_hrdata = {22'h0, opl_free};
            12'h044: regs_hrdata = {20'h0, pcm_free};
            12'h048: regs_hrdata = {8'h0, pcm_period};
            12'h050: regs_hrdata = {8'h0, spk_half};
            12'h054: regs_hrdata = {31'h0, spk_on};
            12'h058: regs_hrdata = {23'h0, volume};
            12'h060: regs_hrdata = {30'h0, ss_load_pend, ss_save_pend};
            12'h064: regs_hrdata = ss_size;
            12'h06c: regs_hrdata = settings_s;
            default: regs_hrdata = 32'h0;
        endcase
    end

    // ---- audio ----------------------------------------------------------
    audio #(.SYS_HZ(SYS_HZ)) u_audio (
        .clk(clk), .rst_n(cpu_rst_n),
        .opl_wr(opl_wr), .opl_data(wd[15:0]), .opl_free(opl_free),
        .pcm_wr(pcm_wr), .pcm_data(wd[7:0]), .pcm_free(pcm_free), .pcm_flush(pcm_flush),
        .pcm_period(pcm_period), .spk_half(spk_half), .spk_on(spk_on), .volume(volume),
        .sample_out(sample));

    // ---- status screen ------------------------------------------------------
    // if the firmware has not written the palette 5 s after power-on, the
    // video shows, as bits (video.v): the run control, the I-cache's line
    // fills and the address of the last one, for when the core does not
    // start on the hardware (before that, and on a normal start, black)
    localparam [28:0] DIAG_WAIT = 5 * SYS_HZ;
    reg [28:0] diag_wait;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) diag_wait <= 0;
        else if (diag_wait != DIAG_WAIT) diag_wait <= diag_wait + 1'b1;
    reg        pal_seen, dc_seen;
    reg [15:0] ic_fills;
    reg [25:4] ic_last;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) begin
            pal_seen <= 1'b0; dc_seen <= 1'b0; ic_fills <= 0; ic_last <= 0;
        end else begin
            if (pal_we) pal_seen <= 1'b1;
            if (dc_ack) dc_seen <= 1'b1;
            if (ic_ack) begin ic_fills <= ic_fills + 1'b1; ic_last <= ic_addr; end
        end
    wire [63:0] diag = {dbg_status, sdram_ready, cpu_rst_n, vram_base != 32'hffff0000, dc_seen, 8'h00,
                        ic_fills,
                        6'h00, ic_last[25:16],
                        ic_last[15:4], 4'h0};

    // ---- video ----------------------------------------------------------
    video u_video (
        .clk_vid(clk_vid), .rst_n(vid_rst_n),
        .vram_addr(vid_vram_addr), .vram_q(vid_vram_q),
        .clk_sys(clk), .pal_we(pal_we), .pal_addr(rg_addr[9:2]), .pal_data(wd[23:0]),
        .diag_en(!pal_seen && diag_wait == DIAG_WAIT), .diag(diag),
        .rgb(vid_rgb), .de(vid_de), .hs(vid_hs), .vs(vid_vs));

    // ---- SDRAM: loader, D-cache, I-cache --------------------------------
    // the loader writes one word: a line write with that word's bytes enabled
    wire ld_ack;
    reg  ld_busy;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) ld_busy <= 1'b0;
        else if (ld_ack) ld_busy <= 1'b0;
        else if (ld_valid && !ld_busy) ld_busy <= 1'b1;
    assign ld_ready = ld_ack;           // the word is written: next one
    wire [15:0] ld_mask = 16'h000f << (ld_addr[3:2] * 4);

    // ---- the staging area's reader: bridge reads, one 32-bit word at a time,
    // usually in order, with a word due by the bridge's next read (about a
    // microsecond).  Two lines are kept: the one being read and the next,
    // fetched ahead; a read elsewhere fetches its line (and the next) first.
    // Loader writes and a new blob drop both (the new blob prefetches its
    // first two lines).
    reg  [20:0]  c_tag, n_tag, f_tag;
    reg          c_v, n_v;
    reg  [127:0] c_dat, n_dat;
    reg          rq, pend, f_busy, f_slot;
    reg  [22:0]  rq_addr;
    reg  [1:0]   pend_w;
    reg  [7:0]   gen, f_gen;
    wire         r_ack;
    wire         ss_inval = ss_save_done || ld_ack;
    wire [20:0]  rq_line = rq_addr[22:2];
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            c_tag <= 0; n_tag <= 21'd1; c_v <= 1'b0; n_v <= 1'b0; rq <= 1'b0; pend <= 1'b0;
            f_busy <= 1'b0; gen <= 0; ss_rd_data <= 0;
        end else begin
            if (ss_inval) begin
                c_tag <= 0; n_tag <= 21'd1; c_v <= 1'b0; n_v <= 1'b0; pend <= 1'b0;
                gen <= gen + 1'b1;
                if (r_ack) f_busy <= 1'b0;
            end else if (r_ack) begin                   // a line arrives
                f_busy <= 1'b0;
                if (f_gen == gen) begin
                    if (!f_slot && c_tag == f_tag && !c_v) begin
                        c_dat <= line_rdata; c_v <= 1'b1;
                        if (pend) begin ss_rd_data <= line_rdata[32*pend_w +: 32]; pend <= 1'b0; end
                    end else if (f_slot && n_tag == f_tag && !n_v) begin
                        n_dat <= line_rdata; n_v <= 1'b1;
                    end
                end
            end else if (rq) begin                      // a bridge read
                rq <= 1'b0;
                if (c_v && c_tag == rq_line) ss_rd_data <= c_dat[32*rq_addr[1:0] +: 32];
                else if (n_v && n_tag == rq_line) begin
                    ss_rd_data <= n_dat[32*rq_addr[1:0] +: 32];
                    c_tag <= n_tag; c_dat <= n_dat; c_v <= 1'b1;
                    n_tag <= rq_line + 1'b1; n_v <= 1'b0;
                end else begin
                    c_tag <= rq_line; c_v <= 1'b0; n_tag <= rq_line + 1'b1; n_v <= 1'b0;
                    pend <= 1'b1; pend_w <= rq_addr[1:0];
                end
            end else if (!f_busy) begin                 // fetch what is missing
                if (!c_v) begin f_busy <= 1'b1; f_slot <= 1'b0; f_tag <= c_tag; f_gen <= gen; end
                else if (!n_v) begin f_busy <= 1'b1; f_slot <= 1'b1; f_tag <= n_tag; f_gen <= gen; end
            end
            if (ss_rd_req) begin rq <= 1'b1; rq_addr <= ss_rd_addr; end
        end
    end

    wire         m_req, m_we, m_ack;
    wire [25:4]  m_addr;
    wire [127:0] m_wdata, m_rdata;
    wire [15:0]  m_wmask;
    line_arb u_arb (
        .clk(clk), .rst_n(rst_n),
        .l_req(ld_valid && ld_busy), .l_addr(ld_addr[25:4]), .l_wdata({4{ld_data}}),
        .l_wmask(ld_mask), .l_ack(ld_ack),
        .r_req(f_busy), .r_addr({1'b1, f_tag}), .r_ack(r_ack),
        .d_req(dc_req), .d_we(dc_we), .d_addr(dc_addr), .d_wdata(dc_wdata), .d_ack(dc_ack),
        .i_req(ic_req), .i_addr(ic_addr), .i_ack(ic_ack),
        .rdata(line_rdata),
        .m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_wdata(m_wdata), .m_wmask(m_wmask),
        .m_ack(m_ack), .m_rdata(m_rdata));

    sdram_ctrl #(.CLK_MHZ((SYS_HZ + 999999) / 1000000), .CL(SD_CL), .RD_LAT(SD_RD_LAT)) u_sdram (
        .clk(clk), .rst_n(rst_n), .ready(sdram_ready),
        .req(m_req), .we(m_we), .addr(m_addr), .wdata(m_wdata), .wmask(m_wmask),
        .ack(m_ack), .rdata(m_rdata),
        .sd_cke(sd_cke), .sd_ba(sd_ba), .sd_a(sd_a), .sd_ras_n(sd_ras_n), .sd_cas_n(sd_cas_n),
        .sd_we_n(sd_we_n), .sd_dqm(sd_dqm), .sd_dq_out(sd_dq_out), .sd_dq_oe(sd_dq_oe),
        .sd_dq_in(sd_dq_in));
endmodule
