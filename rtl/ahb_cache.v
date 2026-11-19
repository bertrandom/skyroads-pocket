// Direct-mapped cache between an AHB-Lite master port of the CPU and the
// SDRAM's line interface.  16-byte lines, zero wait states on a hit.
//
//   WRITABLE=1  data cache: write-back, write-allocate
//   WRITABLE=0  instruction cache: read only
//
// Timing: the tag and data RAMs are read with the address-phase address
// (haddr), so in the data phase the hit is known and the word is there.
// A write hit writes the RAM in its data phase; the next access's read of
// the same word or tag (issued in that same cycle) is fixed up by the
// one-cycle forwarding registers fw_* / tw_*.  A miss holds hready low,
// writes the victim back if it is dirty, fills the line, then completes
// from the line buffer (S_DONE), the cycle in which the next address phase
// is taken and the RAMs are read again with haddr.
//
// The cacheable space is 64 MiB: haddr[25:0].  The tags are cleared after
// every reset (S_INIT, one set per cycle) since the SDRAM is reloaded
// behind the cache's back while the CPU is held in reset.
module ahb_cache #(
    parameter SETS_LOG2 = 11,           // 2048 lines = 32 KiB
    parameter WRITABLE  = 1
) (
    input  wire         clk,
    input  wire         rst_n,

    // AHB-Lite slave
    input  wire         sel,            // address phase: htrans[1] && decoded
    input  wire [31:0]  haddr,
    input  wire         hwrite,
    input  wire [2:0]   hsize,
    input  wire [31:0]  hwdata,
    input  wire         hready_in,      // the bus's hready (address phase taken)
    output reg          hready_out,
    output reg  [31:0]  hrdata,

    // line interface to the SDRAM (via line_arb)
    output reg          mem_req,
    output reg          mem_we,
    output reg  [25:4]  mem_addr,
    output reg  [127:0] mem_wdata,
    input  wire         mem_ack,
    input  wire [127:0] mem_rdata
);
    localparam SETS = 1 << SETS_LOG2;
    localparam TW   = 22 - SETS_LOG2;   // tag bits: haddr[25:4+SETS_LOG2]

    // ---- RAMs -------------------------------------------------------
    reg [31:0]    dram [0:SETS*4-1];
    reg [TW+1:0]  tram [0:SETS-1];      // {valid, dirty, tag}

    reg  [SETS_LOG2+1:0] d_raddr, d_waddr;
    reg  [31:0]          d_wdata;
    reg  [3:0]           d_we;
    reg  [31:0]          d_q;
    reg  [SETS_LOG2-1:0] t_raddr, t_waddr;
    reg  [TW+1:0]        t_wdata;
    reg                  t_we;
    reg  [TW+1:0]        t_q;

    always @(posedge clk) begin
        if (d_we[0]) dram[d_waddr][7:0]   <= d_wdata[7:0];
        if (d_we[1]) dram[d_waddr][15:8]  <= d_wdata[15:8];
        if (d_we[2]) dram[d_waddr][23:16] <= d_wdata[23:16];
        if (d_we[3]) dram[d_waddr][31:24] <= d_wdata[31:24];
        d_q <= dram[d_raddr];
    end
    always @(posedge clk) begin
        if (t_we) tram[t_waddr] <= t_wdata;
        t_q <= tram[t_raddr];
    end

    // ---- data phase -------------------------------------------------
    reg          dp_act, dp_write;
    reg  [25:0]  dp_addr;
    reg  [2:0]   dp_size;
    wire [1:0]            dp_word = dp_addr[3:2];
    wire [SETS_LOG2-1:0]  dp_idx  = dp_addr[4 +: SETS_LOG2];
    wire [TW-1:0]         dp_tag  = dp_addr[25 -: TW];

    // forwarding of the previous cycle's RAM writes
    reg                  fw_v;
    reg [SETS_LOG2+1:0]  fw_addr;
    reg [31:0]           fw_data;
    reg [3:0]            fw_be;
    reg                  tw_v;
    reg [SETS_LOG2-1:0]  tw_idx;
    reg [TW+1:0]         tw_val;

    wire [TW+1:0] tag_eff = (tw_v && tw_idx == dp_idx) ? tw_val : t_q;
    wire          hit     = tag_eff[TW+1] && tag_eff[TW-1:0] == dp_tag;
    wire          fwd     = fw_v && fw_addr == {dp_idx, dp_word};
    wire [31:0]   word_eff = {
        (fwd && fw_be[3]) ? fw_data[31:24] : d_q[31:24],
        (fwd && fw_be[2]) ? fw_data[23:16] : d_q[23:16],
        (fwd && fw_be[1]) ? fw_data[15:8]  : d_q[15:8],
        (fwd && fw_be[0]) ? fw_data[7:0]   : d_q[7:0] };

    wire [3:0] dp_be = dp_size[1] ? 4'b1111 :
                       dp_size[0] ? (dp_addr[1] ? 4'b1100 : 4'b0011) :
                                    (4'b0001 << dp_addr[1:0]);

    // ---- state machine ----------------------------------------------
    localparam S_INIT = 3'd0, S_IDLE = 3'd1, S_WB_READ = 3'd2, S_WB = 3'd3,
               S_FILL = 3'd4, S_FILL_WR = 3'd5, S_DONE = 3'd6;
    reg [2:0]            state;
    reg [SETS_LOG2-1:0]  init_cnt;
    reg [2:0]            cnt;
    reg [127:0]          line;
    reg [TW-1:0]         vic_tag;

    wire idle_hit = state == S_IDLE && dp_act && hit;

    always @(*) begin
        hready_out = state == S_DONE || (state == S_IDLE && (!dp_act || hit));
        hrdata = state == S_DONE ? line[dp_word*32 +: 32] : word_eff;
    end

    // RAM read addresses: the address phase's when it is being taken,
    // otherwise the victim's words
    always @(*) begin
        if (state == S_WB_READ) begin
            d_raddr = {dp_idx, cnt[1:0]};
            t_raddr = dp_idx;
        end else begin
            d_raddr = {haddr[4 +: SETS_LOG2], haddr[3:2]};
            t_raddr = haddr[4 +: SETS_LOG2];
        end
    end

    // merge the pending write into a word of the line
    function [31:0] merge(input [31:0] old, input [31:0] wd, input [3:0] be);
        merge = { be[3] ? wd[31:24] : old[31:24], be[2] ? wd[23:16] : old[23:16],
                  be[1] ? wd[15:8]  : old[15:8],  be[0] ? wd[7:0]   : old[7:0] };
    endfunction

    // RAM writes (combinational from the state, registered in the RAM)
    always @(*) begin
        d_we = 4'b0; d_waddr = {dp_idx, dp_word}; d_wdata = hwdata;
        t_we = 1'b0; t_waddr = dp_idx; t_wdata = {1'b1, 1'b1, dp_tag};
        case (state)
            S_INIT: begin
                t_we = 1'b1; t_waddr = init_cnt; t_wdata = 0;
            end
            S_IDLE: if (WRITABLE && idle_hit && dp_write) begin
                d_we = dp_be;
                t_we = !tag_eff[TW];    // first write: mark dirty
            end
            S_FILL_WR: begin
                d_we = 4'b1111;
                d_waddr = {dp_idx, cnt[1:0]};
                d_wdata = line[cnt[1:0]*32 +: 32];
                t_we = cnt[1:0] == 2'd3;
                t_wdata = {1'b1, WRITABLE && dp_write, dp_tag};
            end
            default: ;
        endcase
    end

    always @(posedge clk) begin
        fw_v <= |d_we; fw_addr <= d_waddr; fw_data <= d_wdata; fw_be <= d_we;
        tw_v <= t_we;  tw_idx <= t_waddr;  tw_val <= t_wdata;
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= S_INIT;
            init_cnt <= 0;
            dp_act <= 1'b0;
            mem_req <= 1'b0;
            mem_we <= 1'b0;
            cnt <= 0;
        end else begin
            // the address phase is taken whenever the bus's hready is high
            if (hready_in) begin
                dp_act   <= sel;
                dp_addr  <= haddr[25:0];
                dp_write <= hwrite;
                dp_size  <= hsize;
            end
            case (state)
                S_INIT: begin
                    init_cnt <= init_cnt + 1'b1;
                    if (&init_cnt) state <= S_IDLE;
                end
                S_IDLE: if (dp_act && !hit) begin
                    vic_tag <= tag_eff[TW-1:0];
                    cnt <= 0;
                    if (WRITABLE && tag_eff[TW+1] && tag_eff[TW]) state <= S_WB_READ;
                    else begin
                        state <= S_FILL;
                        mem_req <= 1'b1; mem_we <= 1'b0;
                        mem_addr <= dp_addr[25:4];
                    end
                end
                S_WB_READ: begin        // words appear one cycle after their address
                    cnt <= cnt + 1'b1;
                    if (cnt != 0) line[(cnt - 1'b1) * 32 +: 32] <= d_q;
                    if (cnt == 3'd4) begin
                        state <= S_WB;
                        mem_req <= 1'b1; mem_we <= 1'b1;
                        mem_addr <= {vic_tag, dp_idx};
                        mem_wdata <= {d_q, line[95:0]};
                    end
                end
                S_WB: if (mem_ack) begin
                    state <= S_FILL;
                    mem_we <= 1'b0;
                    mem_addr <= dp_addr[25:4];
                end
                S_FILL: if (mem_ack) begin
                    mem_req <= 1'b0;
                    line <= mem_rdata;
                    if (WRITABLE && dp_write)
                        line[dp_word*32 +: 32] <= merge(mem_rdata[dp_word*32 +: 32], hwdata, dp_be);
                    cnt <= 0;
                    state <= S_FILL_WR;
                end
                S_FILL_WR: begin
                    cnt <= cnt + 1'b1;
                    if (cnt[1:0] == 2'd3) state <= S_DONE;
                end
                S_DONE: state <= S_IDLE;
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule
