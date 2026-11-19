// Stand-ins for the Altera primitives, for linting core_top with Verilator.
module mf_pllbase(input wire refclk, input wire rst, output wire outclk_0, output wire outclk_1,
                  output wire outclk_2, output wire outclk_3, output wire outclk_4, output wire locked);
    assign {outclk_0, outclk_1, outclk_2, outclk_3, outclk_4} = {5{refclk}};
    assign locked = 1'b1;
endmodule
module pin_ddio_clk(input wire [0:0] datain_h, input wire [0:0] datain_l, input wire outclock,
                    output wire [0:0] dataout);
    assign dataout = outclock ? datain_h : datain_l;
endmodule
module mf_datatable(input wire [7:0] address_a, input wire [7:0] address_b, input wire clock_a,
                    input wire clock_b, input wire [31:0] data_a, input wire [31:0] data_b,
                    input wire wren_a, input wire wren_b, output reg [31:0] q_a, output reg [31:0] q_b);
    reg [31:0] mem [0:255];
    always @(posedge clock_a) begin if (wren_a) mem[address_a] <= data_a; q_a <= mem[address_a]; end
    always @(posedge clock_b) begin if (wren_b) mem[address_b] <= data_b; q_b <= mem[address_b]; end
endmodule
