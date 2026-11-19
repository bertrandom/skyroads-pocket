// Shares the SDRAM's line interface between the bridge loader (writes),
// the save-state reader (bridge reads of the staging area), the data cache
// and the instruction cache, in that priority.  A grant lasts until the controller's ack; requests are looked
// at again from the cycle after it, so a cache that keeps req high and
// changes the request on the ack (write-back then fill) is served twice.
module line_arb (
    input  wire         clk,
    input  wire         rst_n,

    input  wire         l_req,
    input  wire [25:4]  l_addr,
    input  wire [127:0] l_wdata,
    input  wire [15:0]  l_wmask,
    output wire         l_ack,

    input  wire         r_req,
    input  wire [25:4]  r_addr,
    output wire         r_ack,

    input  wire         d_req,
    input  wire         d_we,
    input  wire [25:4]  d_addr,
    input  wire [127:0] d_wdata,
    output wire         d_ack,

    input  wire         i_req,
    input  wire [25:4]  i_addr,
    output wire         i_ack,

    output wire [127:0] rdata,          // to both caches

    output reg          m_req,
    output reg          m_we,
    output reg  [25:4]  m_addr,
    output reg  [127:0] m_wdata,
    output reg  [15:0]  m_wmask,
    input  wire         m_ack,
    input  wire [127:0] m_rdata
);
    reg [2:0] owner;                    // 0 none, 1 loader, 2 data, 3 instr, 4 reader
    assign l_ack = m_ack && owner == 3'd1;
    assign d_ack = m_ack && owner == 3'd2;
    assign i_ack = m_ack && owner == 3'd3;
    assign r_ack = m_ack && owner == 3'd4;
    assign rdata = m_rdata;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            owner <= 3'd0;
            m_req <= 1'b0;
            m_we <= 1'b0;
        end else if (owner == 3'd0) begin
            if (l_req) begin
                owner <= 3'd1; m_req <= 1'b1; m_we <= 1'b1;
                m_addr <= l_addr; m_wdata <= l_wdata; m_wmask <= l_wmask;
            end else if (r_req) begin
                owner <= 3'd4; m_req <= 1'b1; m_we <= 1'b0;
                m_addr <= r_addr; m_wmask <= 16'hffff;
            end else if (d_req) begin
                owner <= 3'd2; m_req <= 1'b1; m_we <= d_we;
                m_addr <= d_addr; m_wdata <= d_wdata; m_wmask <= 16'hffff;
            end else if (i_req) begin
                owner <= 3'd3; m_req <= 1'b1; m_we <= 1'b0;
                m_addr <= i_addr; m_wmask <= 16'hffff;
            end
        end else begin
            // held until done: the controller takes it when it is idle
            // (not refreshing), and is busy until after its ack
            if (m_ack) begin owner <= 3'd0; m_req <= 1'b0; end
        end
    end
endmodule
