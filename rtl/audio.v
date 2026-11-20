// Sound: the AdLib (an OPL2, jtopl2), the Sound Blaster's 8-bit samples
// and the PC speaker, mixed like the native port's mixer (port/audio.c):
//   out = (opl * 2 + (sb - 128) * 138 + speaker * 3840) * volume / 256
// sample_out is the mix, updated continuously in the system clock domain.
module audio #(
    parameter SYS_HZ = 50000000
) (
    input  wire         clk,
    input  wire         rst_n,

    input  wire         opl_wr,         // queue an OPL write: reg << 8 | value
    input  wire [15:0]  opl_data,
    output wire [9:0]   opl_free,

    input  wire         pcm_wr,
    input  wire [7:0]   pcm_data,
    output wire [11:0]  pcm_free,
    input  wire         pcm_flush,
    input  wire [23:0]  pcm_period,     // cycles per sample

    input  wire [23:0]  spk_half,       // PC speaker half period, cycles; 0 silent
    input  wire         spk_on,
    input  wire [8:0]   volume,         // 256 = unity

    output reg signed [15:0] sample_out
);
    // ---- OPL2 ---------------------------------------------------------
    // 3.579545 MHz clock enable from the system clock
    localparam OPL_HZ = 3579545;
    reg [31:0] cen_acc;
    reg        opl_cen;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin cen_acc <= 0; opl_cen <= 1'b0; end
        else if (cen_acc + OPL_HZ >= SYS_HZ) begin cen_acc <= cen_acc + OPL_HZ - SYS_HZ; opl_cen <= 1'b1; end
        else begin cen_acc <= cen_acc + OPL_HZ; opl_cen <= 1'b0; end
    end

    // jtopl wants its reset held for a while with clock enables running
    reg [7:0] opl_rst_cnt;
    wire      opl_rst = opl_rst_cnt != 8'hff;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) opl_rst_cnt <= 0;
        else if (opl_rst && opl_cen) opl_rst_cnt <= opl_rst_cnt + 1'b1;

    wire [15:0] oq_dout;
    wire        oq_empty;
    reg         oq_pop;
    sync_fifo #(.W(16), .DEPTH_LOG2(9)) u_oplq (
        .clk(clk), .rst_n(rst_n), .flush(1'b0),
        .push(opl_wr), .din(opl_data),
        .pop(oq_pop), .dout(oq_dout), .empty(oq_empty), .free(opl_free));

    // 96 chip clocks (~27 us) after a data write before the next write, so
    // that every write, a key off followed by a key on included, is seen.
    // jtopl takes an operator write over four slots after the operator's
    // own (TL, the multiplier and AM at stages II and IV, jtopl_csr.v) and
    // any write, the next address included, clears that pipeline: the gap
    // must be a whole sample period (72 chip clocks, all 18 slots) plus the
    // three slots (12 clocks), in chip clocks.  It was a sample period plus
    // 16 system clocks: enough with the chip clocked every cycle (sim/
    // tb_songs.cpp) but ~73 chip clocks at 49.152 MHz, and a write whose
    // slot came round late lost its TL or multiplier (a voice missing or
    // wrong, differently from one play to the next)
    localparam [63:0] GAP64 = 64'd96 * SYS_HZ / OPL_HZ;
    localparam GAP = GAP64[15:0];
    localparam GAP_ADDR = SYS_HZ / (OPL_HZ / 12) + 4;   // after the address write
    reg [15:0] gap;
    reg [1:0]  oseq;
    reg        o_cs_n, o_addr;
    reg [7:0]  o_din;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            oseq <= 0; gap <= 0; oq_pop <= 1'b0; o_cs_n <= 1'b1; o_addr <= 1'b0; o_din <= 0;
        end else begin
            oq_pop <= 1'b0;
            o_cs_n <= 1'b1;
            if (gap != 0) gap <= gap - 1'b1;
            case (oseq)
                2'd0: if (gap == 0 && !oq_empty && !opl_rst) begin
                    o_cs_n <= 1'b0; o_addr <= 1'b0; o_din <= oq_dout[15:8];
                    gap <= GAP_ADDR[15:0];
                    oseq <= 2'd1;
                end
                2'd1: if (gap == 0) begin
                    o_cs_n <= 1'b0; o_addr <= 1'b1; o_din <= oq_dout[7:0];
                    oq_pop <= 1'b1;
                    gap <= GAP[15:0];
                    oseq <= 2'd0;
                end
                default: oseq <= 2'd0;
            endcase
        end
    end

    wire signed [15:0] opl_snd;
    wire               opl_sample;
    wire [7:0]         opl_dout;
    wire               opl_irq_n;
    jtopl2 u_opl (
        .rst(opl_rst), .clk(clk), .cen(opl_cen),
        .din(o_din), .addr(o_addr), .cs_n(o_cs_n), .wr_n(o_cs_n),
        .dout(opl_dout), .irq_n(opl_irq_n),
        .snd(opl_snd), .sample(opl_sample));

    // ---- the OPL's output, resampled -----------------------------------
    // The chip makes a sample every 72 of its clocks (49,716 Hz), the I2S
    // side takes one at 48 kHz whenever it likes (core_top.v).  Taking the
    // chip's latest sample then drops one in 29, unevenly: a rough edge on
    // high notes.  So the output is a line between the chip's last two
    // samples, at how far the system clock is into the current period
    // (a sample late: linear interpolation).
    localparam [63:0] FRAC_K = 64'd16777216 * OPL_HZ / (64'd72 * SYS_HZ);  // 65536 * 256 / period
    reg               opl_sample_d;
    reg signed [15:0] opl_prev, opl_cur;
    reg        [10:0] opl_age;          // system clocks since the chip's last sample
    reg        [16:0] opl_frac;         // 0 .. 65536
    reg signed [16:0] opl_diff;
    reg signed [34:0] opl_step;
    reg signed [15:0] opl_out;
    wire       [31:0] opl_pos = {21'd0, opl_age} * FRAC_K[31:0];   // 65536 * 256 per period
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            opl_sample_d <= 1'b0; opl_prev <= 0; opl_cur <= 0; opl_age <= 0;
            opl_frac <= 0; opl_diff <= 0; opl_step <= 0; opl_out <= 0;
        end else begin
            opl_sample_d <= opl_sample;
            if (opl_sample_d && !opl_sample) begin  // the chip's output was updated
                opl_prev <= opl_cur; opl_cur <= opl_snd; opl_age <= 0;
            end else if (opl_age != 11'h7ff) opl_age <= opl_age + 1'b1;
            // pipelined: fraction, difference, product, sum
            opl_frac <= opl_pos[31:24] != 0 ? 17'd65536 : opl_pos[24:8];
            opl_diff <= opl_cur - opl_prev;
            opl_step <= opl_diff * $signed({1'b0, opl_frac});
            opl_out  <= opl_prev + opl_step[32:16];    // between the two: fits
        end
    end

    // ---- Sound Blaster samples ----------------------------------------
    wire [7:0] pq_dout;
    wire       pq_empty;
    reg        pq_pop;
    reg [23:0] pcm_cnt;
    reg [7:0]  pcm_cur;
    reg        pcm_load;
    sync_fifo #(.W(8), .DEPTH_LOG2(11)) u_pcmq (
        .clk(clk), .rst_n(rst_n), .flush(pcm_flush),
        .push(pcm_wr), .din(pcm_data),
        .pop(pq_pop), .dout(pq_dout), .empty(pq_empty), .free(pcm_free));
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pcm_cnt <= 0; pcm_cur <= 8'h80; pq_pop <= 1'b0; pcm_load <= 1'b0;
        end else begin
            pq_pop <= 1'b0;
            if (pcm_flush) begin pcm_cur <= 8'h80; pcm_cnt <= 0; end
            else if (pcm_cnt != 0) pcm_cnt <= pcm_cnt - 1'b1;
            else begin
                pcm_cnt <= pcm_period;
                if (!pq_empty) begin pcm_cur <= pq_dout; pq_pop <= 1'b1; end
                else pcm_cur <= 8'h80;          // played out: silence
            end
        end
    end

    // ---- PC speaker ---------------------------------------------------
    reg [23:0] spk_cnt;
    reg        spk_level;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin spk_cnt <= 0; spk_level <= 1'b0; end
        else if (spk_half == 0) spk_cnt <= 0;
        else if (spk_cnt >= spk_half) begin spk_cnt <= 0; spk_level <= ~spk_level; end
        else spk_cnt <= spk_cnt + 1'b1;
    end
    wire spk_active = spk_on && spk_half != 0;

    // ---- mix ----------------------------------------------------------
    reg signed [19:0] mix;
    reg signed [28:0] scaled;
    always @(posedge clk) begin
        mix <= $signed({{3{opl_out[15]}}, opl_out, 1'b0})
             + $signed({{12{1'b0}}, pcm_cur}) * 20'sd138 - 20'sd17664
             + (spk_active ? (spk_level ? 20'sd3840 : -20'sd3840) : 20'sd0);
        scaled <= mix * $signed({1'b0, volume});
        if (scaled > 29'sd8388352)        sample_out <= 16'sh7fff;   // 32767 * 256
        else if (scaled < -29'sd8388608)  sample_out <= -16'sh8000;
        else                              sample_out <= scaled[23:8];
    end
endmodule
