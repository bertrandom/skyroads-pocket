// SDRAM controller for the Pocket's 64 MiB x16 SDRAM (8192 rows, 1024
// columns, 4 banks).  It moves whole 16-byte lines: one burst of 8 with
// auto precharge per request, no open-row tricks.  Writes take a byte mask
// (the bridge loader writes single words).
//
// Line address bits [25:4]: column = [10:4] (x8 half words), row = [23:11],
// bank = [25:24].
//
// RD_LAT: cycles from the READ command leaving the command register to the
// first half word arriving in the input register (sd_dq_in is registered in
// the pad logic).  core_top.v clocks the SDRAM with the inverted system
// clock: the chip takes each command half a cycle after it is registered and
// its data is caught by the pad register CL cycles later: CL + 1.  The
// testbench models exactly that; on the hardware it is the first thing to
// check (with a logic analyser or a memory test) if the CPU does not start.
module sdram_ctrl #(
    parameter CLK_MHZ = 50,
    parameter CL      = 2,
    parameter RD_LAT  = CL + 1
) (
    input  wire         clk,
    input  wire         rst_n,
    output reg          ready,          // init done

    input  wire         req,
    input  wire         we,
    input  wire [25:4]  addr,
    input  wire [127:0] wdata,
    input  wire [15:0]  wmask,          // byte enables
    output reg          ack,            // one cycle; rdata valid with it
    output reg  [127:0] rdata,

    output reg          sd_cke,
    output reg  [1:0]   sd_ba,
    output reg  [12:0]  sd_a,
    output reg          sd_ras_n,
    output reg          sd_cas_n,
    output reg          sd_we_n,
    output reg  [1:0]   sd_dqm,
    output reg  [15:0]  sd_dq_out,
    output reg          sd_dq_oe,
    input  wire [15:0]  sd_dq_in        // registered at the pad
);
    // timings in cycles (rounded up), minimum 1
    function integer cyc(input integer ns);
        cyc = (ns * CLK_MHZ + 999) / 1000;
        if (cyc < 1) cyc = 1;
    endfunction
    localparam T_POWERUP = CLK_MHZ * 200;       // 200 us
    localparam T_RCD = cyc(18), T_RP = cyc(18), T_RFC = cyc(66), T_WR = cyc(15) + 1;
    localparam T_MRD = 2;
    localparam T_REFI = CLK_MHZ * 15 / 2;       // 7.5 us (64 ms / 8192 rows = 7.8)

    localparam [2:0] C_NOP = 3'b111, C_ACT = 3'b011, C_READ = 3'b101, C_WRITE = 3'b100,
                     C_PRE = 3'b010, C_REF = 3'b001, C_MRS = 3'b000;

    localparam S_POWERUP = 4'd0, S_PREALL = 4'd1, S_INITREF = 4'd2, S_MRS = 4'd3,
               S_IDLE = 4'd4, S_ACT = 4'd5, S_RW = 4'd6, S_RDATA = 4'd7,
               S_WDATA = 4'd8, S_WAIT = 4'd9, S_REF = 4'd10;

    reg [3:0]   state;
    reg [15:0]  timer;
    reg [3:0]   initref;
    reg [15:0]  refcnt;
    reg         ref_due;
    reg         op_we;
    reg [25:4]  op_addr;
    reg [127:0] op_wdata;
    reg [15:0]  op_mask;
    reg [3:0]   beat;
    reg [4:0]   rd_wait;

    task cmd(input [2:0] c);
        {sd_ras_n, sd_cas_n, sd_we_n} = c;
    endtask

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= S_POWERUP;
            timer <= T_POWERUP[15:0];
            ready <= 1'b0;
            ack <= 1'b0;
            sd_cke <= 1'b0;
            {sd_ras_n, sd_cas_n, sd_we_n} <= C_NOP;
            sd_dqm <= 2'b11;
            sd_dq_oe <= 1'b0;
            sd_a <= 0; sd_ba <= 0; sd_dq_out <= 0;
            refcnt <= 0; ref_due <= 1'b0;
            initref <= 0;
        end else begin
            ack <= 1'b0;
            {sd_ras_n, sd_cas_n, sd_we_n} <= C_NOP;
            sd_dq_oe <= 1'b0;
            if (timer != 0) timer <= timer - 1'b1;

            if (ready) begin
                if (refcnt == T_REFI[15:0]) begin refcnt <= 0; ref_due <= 1'b1; end
                else refcnt <= refcnt + 1'b1;
            end

            case (state)
                S_POWERUP: begin
                    sd_cke <= 1'b1;
                    if (timer == 0) begin
                        {sd_ras_n, sd_cas_n, sd_we_n} <= C_PRE;
                        sd_a[10] <= 1'b1;               // all banks
                        timer <= T_RP;
                        state <= S_PREALL;
                    end
                end
                S_PREALL: if (timer == 0) begin
                    initref <= 4'd8;
                    state <= S_INITREF;
                end
                S_INITREF: if (timer == 0) begin
                    if (initref == 0) begin
                        {sd_ras_n, sd_cas_n, sd_we_n} <= C_MRS;
                        sd_ba <= 2'b00;
                        // burst write, CL, sequential, burst length 8
                        sd_a <= {3'b000, 1'b0, 2'b00, CL[2:0], 1'b0, 3'b011};
                        timer <= T_MRD;
                        state <= S_MRS;
                    end else begin
                        {sd_ras_n, sd_cas_n, sd_we_n} <= C_REF;
                        initref <= initref - 1'b1;
                        timer <= T_RFC;
                    end
                end
                S_MRS: if (timer == 0) begin
                    ready <= 1'b1;
                    sd_dqm <= 2'b00;
                    state <= S_IDLE;
                end

                S_IDLE: begin
                    if (ref_due) begin
                        ref_due <= 1'b0;
                        {sd_ras_n, sd_cas_n, sd_we_n} <= C_REF;
                        timer <= T_RFC;
                        state <= S_WAIT;
                    end else if (req) begin
                        op_we <= we;
                        op_addr <= addr;
                        op_wdata <= wdata;
                        op_mask <= wmask;
                        {sd_ras_n, sd_cas_n, sd_we_n} <= C_ACT;
                        sd_ba <= addr[25:24];
                        sd_a <= addr[23:11];
                        timer <= T_RCD - 1;
                        state <= S_RW;
                    end
                end
                S_RW: if (timer == 0) begin
                    sd_a <= {2'b00, 1'b1, op_addr[10:4], 3'b000};   // A10: auto precharge
                    beat <= 0;
                    if (op_we) begin
                        {sd_ras_n, sd_cas_n, sd_we_n} <= C_WRITE;
                        sd_dq_oe <= 1'b1;
                        sd_dq_out <= op_wdata[15:0];
                        sd_dqm <= ~op_mask[1:0];
                        beat <= 1;
                        state <= S_WDATA;
                    end else begin
                        {sd_ras_n, sd_cas_n, sd_we_n} <= C_READ;
                        sd_dqm <= 2'b00;
                        rd_wait <= RD_LAT[4:0];
                        state <= S_RDATA;
                    end
                end
                S_WDATA: begin
                    if (beat == 4'd8) begin
                        sd_dqm <= 2'b00;
                        timer <= T_WR + T_RP;
                        state <= S_WAIT;
                        ack <= 1'b1;
                    end else begin
                        sd_dq_oe <= 1'b1;
                        sd_dq_out <= op_wdata[beat*16 +: 16];
                        sd_dqm <= ~op_mask[beat*2 +: 2];
                        beat <= beat + 1'b1;
                    end
                end
                S_RDATA: begin
                    if (rd_wait != 0) rd_wait <= rd_wait - 1'b1;
                    else begin
                        rdata[beat*16 +: 16] <= sd_dq_in;
                        beat <= beat + 1'b1;
                        if (beat == 4'd7) begin
                            ack <= 1'b1;
                            timer <= T_RP;
                            state <= S_WAIT;
                        end
                    end
                end
                S_WAIT: if (timer == 0) state <= S_IDLE;
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule
