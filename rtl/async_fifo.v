// Small dual-clock FIFO (Gray-coded pointers, registers for storage):
// carries the bridge loader's writes from clk_74a to the system clock.
// Show-ahead: rdata is the oldest entry while !empty.
module async_fifo #(
    parameter W = 56,
    parameter DEPTH_LOG2 = 4
) (
    input  wire          wclk,
    input  wire          wrst_n,
    input  wire          we,
    input  wire [W-1:0]  wdata,
    output wire          full,

    input  wire          rclk,
    input  wire          rrst_n,
    input  wire          re,
    output wire [W-1:0]  rdata,
    output wire          empty
);
    localparam N = 1 << DEPTH_LOG2;
    reg [W-1:0] mem [0:N-1];
    reg [DEPTH_LOG2:0] wbin, wgray, rbin, rgray;
    reg [DEPTH_LOG2:0] rgray_w1, rgray_w2, wgray_r1, wgray_r2;

    function [DEPTH_LOG2:0] bin2gray(input [DEPTH_LOG2:0] b); bin2gray = b ^ (b >> 1); endfunction

    wire [DEPTH_LOG2:0] wbin_n = wbin + (we && !full);
    assign full = wgray == {~rgray_w2[DEPTH_LOG2:DEPTH_LOG2-1], rgray_w2[DEPTH_LOG2-2:0]};
    always @(posedge wclk or negedge wrst_n) begin
        if (!wrst_n) begin wbin <= 0; wgray <= 0; rgray_w1 <= 0; rgray_w2 <= 0; end
        else begin
            if (we && !full) mem[wbin[DEPTH_LOG2-1:0]] <= wdata;
            wbin <= wbin_n;
            wgray <= bin2gray(wbin_n);
            {rgray_w2, rgray_w1} <= {rgray_w1, rgray};
        end
    end

    wire [DEPTH_LOG2:0] rbin_n = rbin + (re && !empty);
    assign empty = rgray == wgray_r2;
    assign rdata = mem[rbin[DEPTH_LOG2-1:0]];
    always @(posedge rclk or negedge rrst_n) begin
        if (!rrst_n) begin rbin <= 0; rgray <= 0; wgray_r1 <= 0; wgray_r2 <= 0; end
        else begin
            rbin <= rbin_n;
            rgray <= bin2gray(rbin_n);
            {wgray_r2, wgray_r1} <= {wgray_r1, wgray};
        end
    end
endmodule
