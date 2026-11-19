// Single-clock FIFO in block RAM.  dout is mem[read pointer], registered:
// after a pop, a flush or a push into the empty FIFO it takes two cycles
// to be current, and `empty` stays high meanwhile.  So pop at most every
// third cycle (the users here pop every ~1000 cycles).  A push to a full
// FIFO is dropped.
module sync_fifo #(
    parameter W = 8,
    parameter DEPTH_LOG2 = 11
) (
    input  wire                clk,
    input  wire                rst_n,
    input  wire                flush,
    input  wire                push,
    input  wire [W-1:0]        din,
    input  wire                pop,
    output reg  [W-1:0]        dout,
    output wire                empty,
    output wire [DEPTH_LOG2:0] free
);
    reg [W-1:0] mem [0:(1<<DEPTH_LOG2)-1];
    reg [DEPTH_LOG2-1:0] wp, rp;
    reg [DEPTH_LOG2:0]   count;
    reg [1:0]            stale;         // cycles until dout is mem[rp]
    wire full    = count[DEPTH_LOG2];
    wire do_push = push && !full;
    wire do_pop  = pop && count != 0 && stale == 0;
    assign free  = (1 << DEPTH_LOG2) - count;
    assign empty = count == 0 || stale != 0;

    always @(posedge clk) begin
        if (do_push) mem[wp] <= din;
        dout <= mem[rp];
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wp <= 0; rp <= 0; count <= 0; stale <= 2'd2;
        end else if (flush) begin
            wp <= 0; rp <= 0; count <= 0; stale <= 2'd2;
        end else begin
            if (do_push) wp <= wp + 1'b1;
            if (do_pop) rp <= rp + 1'b1;
            count <= count + do_push - do_pop;
            if (do_pop || (do_push && count == 0)) stale <= 2'd2;
            else if (stale != 0) stale <= stale - 1'b1;
        end
    end
endmodule
