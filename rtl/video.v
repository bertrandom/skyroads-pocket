// Video out for the APF scaler: 320x200 active in a 400x512 frame at
// 12.288 MHz, 60 Hz (the frame of the core template, 200 lines shown).
// Reads the VGA memory (mode 13h, one byte per pixel, rows of 320) from
// the VRAM's second port and the colours from the palette, which the CPU
// writes in its own clock domain.
module video (
    input  wire         clk_vid,
    input  wire         rst_n,          // synchronous to clk_vid

    output wire [13:0]  vram_addr,      // word address
    input  wire [31:0]  vram_q,         // one cycle later

    input  wire         clk_sys,        // palette writes
    input  wire         pal_we,
    input  wire [7:0]   pal_addr,
    input  wire [23:0]  pal_data,

    input  wire         diag_en,        // system clock: show the status bits
    input  wire [63:0]  diag,           // quasi-static, system clock

    output reg  [23:0]  rgb,
    output reg          de,
    output reg          hs,
    output reg          vs
);
    localparam H_TOTAL = 400, H_START = 10, H_ACTIVE = 320;
    localparam V_TOTAL = 512, V_START = 10, V_ACTIVE = 200;

    reg [23:0] pal [0:255];
    always @(posedge clk_sys) if (pal_we) pal[pal_addr] <= pal_data;

    reg [8:0]  x;
    reg [9:0]  y;
    reg [15:0] pix;                     // index of the pixel at (x - H_START)
    wire       active = x >= H_START && x < H_START + H_ACTIVE &&
                        y >= V_START && y < V_START + V_ACTIVE;

    always @(posedge clk_vid or negedge rst_n) begin
        if (!rst_n) begin
            x <= 0; y <= 0; pix <= 0;
        end else begin
            if (x == H_TOTAL - 1) begin
                x <= 0;
                y <= y == V_TOTAL - 1 ? 10'd0 : y + 1'b1;
            end else x <= x + 1'b1;
            if (y == V_TOTAL - 1 && x == H_TOTAL - 1) pix <= 0;
            else if (active) pix <= pix + 1'b1;
        end
    end
    assign vram_addr = pix[15:2];

    // the status screen: 64 bits, most significant first, in 4 rows of 16
    // boxes 20x40 (green 1, dark red 0, grey borders)
    reg [4:0] dsub;  reg [3:0] dcol;
    reg [5:0] dline; reg [2:0] drow;
    reg [1:0] diag_s;
    always @(posedge clk_vid) begin
        diag_s <= {diag_s[0], diag_en};
        if (!active) begin
            if (x == 0) begin dsub <= 0; dcol <= 0; end
        end else if (dsub == 19) begin dsub <= 0; dcol <= dcol + 1'b1; end
        else dsub <= dsub + 1'b1;
        if (x == H_TOTAL - 1) begin
            if (y < V_START) begin dline <= 0; drow <= 0; end
            else if (dline == 39) begin dline <= 0; drow <= drow + 1'b1; end
            else dline <= dline + 1'b1;
        end
    end
    wire        dbit = diag[~{drow[1:0], dcol}];
    wire [23:0] pat0 = drow[2] ? 24'h000000 :
                       (dsub < 2 || dline < 2) ? 24'h303030 :
                       dbit ? 24'h00d000 : 24'h500000;
    reg  [23:0] pat1, pat2;
    always @(posedge clk_vid) begin pat1 <= pat0; pat2 <= pat1; end

    // stage 1: VRAM word read; stage 2: palette read; stage 3: out
    reg [1:0]  lane1;
    reg [2:0]  act_d, hs_d, vs_d;
    reg [23:0] pal_q;
    wire [7:0] index = vram_q[lane1*8 +: 8];
    always @(posedge clk_vid) begin
        lane1 <= pix[1:0];
        pal_q <= pal[index];
        act_d <= {act_d[1:0], active};
        hs_d  <= {hs_d[1:0], x == 3};
        vs_d  <= {vs_d[1:0], x == 0 && y == 0};
        de  <= act_d[1];
        rgb <= act_d[1] ? (diag_s[1] ? pat2 : pal_q) : 24'h0;
        hs  <= hs_d[1];
        vs  <= vs_d[1];
    end
endmodule
