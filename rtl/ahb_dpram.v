// Block RAM as an AHB-Lite slave with a second port in another clock
// domain: the VRAM (port B: the video scan-out) and the save RAM (port B:
// the APF bridge).
//
// Port A (the CPU) uses one address, the data phase's, so that each byte
// lane is Quartus's true dual-port, dual-clock RAM template (one address
// per port): writes take no wait state, reads one (the RAM's read of the
// data phase address comes out a cycle later).
module ahb_dpram #(
    parameter AW = 14,                  // words
    parameter B_WRITE = 1               // port B writes (0: read only)
) (
    input  wire          clk,
    input  wire          rst_n,
    input  wire          sel,           // address phase, decoded
    input  wire [AW-1:0] word,          // address phase word index
    input  wire [1:0]    lane,          // haddr[1:0]
    input  wire          hwrite,
    input  wire [2:0]    hsize,
    input  wire [31:0]   hwdata,
    input  wire          hready_in,
    output wire          hready_out,    // for this slave's data phase
    output reg  [31:0]   hrdata,

    input  wire          clk_b,
    input  wire [AW-1:0] b_addr,
    input  wire          b_we,
    input  wire [31:0]   b_wdata,
    output reg  [31:0]   b_q
);
    reg          dp_w, dp_r, rd_done;
    reg [AW-1:0] dp_word;
    reg [3:0]    dp_be;

    wire [3:0] be = hsize[1] ? 4'b1111 : hsize[0] ? (lane[1] ? 4'b1100 : 4'b0011) : (4'b0001 << lane);
    wire [3:0] we = dp_w ? dp_be : 4'b0;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dp_w <= 1'b0; dp_r <= 1'b0; rd_done <= 1'b0;
        end else begin
            if (hready_in) begin
                dp_w <= sel && hwrite;
                dp_r <= sel && !hwrite;
                dp_word <= word;
                dp_be <= be;
            end
            rd_done <= dp_r && !rd_done;
        end
    end
    assign hready_out = !(dp_r && !rd_done);

    // Quartus does not infer a RAM written from two clocks: under Quartus
    // the save RAM (port B written by the bridge) is the megafunction itself
`ifdef ALTERA_RESERVED_QIS
    localparam USE_MF = B_WRITE;
`else
    localparam USE_MF = 0;
`endif
    genvar i;
    generate
    if (USE_MF) begin : tdp
        wire [31:0] mf_qa, mf_qb;
        altsyncram #(
            .operation_mode("BIDIR_DUAL_PORT"), .intended_device_family("Cyclone V"),
            .width_a(32), .widthad_a(AW), .numwords_a(1 << AW), .width_byteena_a(4),
            .width_b(32), .widthad_b(AW), .numwords_b(1 << AW), .width_byteena_b(1),
            .outdata_reg_a("UNREGISTERED"), .outdata_reg_b("UNREGISTERED"),
            .address_reg_b("CLOCK1"), .indata_reg_b("CLOCK1"), .wrcontrol_wraddress_reg_b("CLOCK1"),
            .read_during_write_mode_port_a("NEW_DATA_NO_NBE_READ"),
            .read_during_write_mode_port_b("NEW_DATA_NO_NBE_READ"),
            .power_up_uninitialized("FALSE"), .ram_block_type("M10K"), .lpm_type("altsyncram")
        ) ram (
            .clock0(clk), .address_a(dp_word), .data_a(hwdata), .wren_a(|we), .byteena_a(we), .q_a(mf_qa),
            .clock1(clk_b), .address_b(b_addr), .data_b(b_wdata), .wren_b(b_we), .q_b(mf_qb),
            .aclr0(1'b0), .aclr1(1'b0), .addressstall_a(1'b0), .addressstall_b(1'b0),
            .byteena_b(1'b1), .clocken0(1'b1), .clocken1(1'b1), .clocken2(1'b1), .clocken3(1'b1),
            .rden_a(1'b1), .rden_b(1'b1), .eccstatus());
        always @(*) begin hrdata = mf_qa; b_q = mf_qb; end
    end else begin : model
        for (i = 0; i < 4; i = i + 1) begin : lane_ram
            reg [7:0] mem [0:(1<<AW)-1];
            reg [7:0] qa, qb;
            always @(posedge clk)
                if (we[i]) begin
                    mem[dp_word] <= hwdata[8*i +: 8];
                    qa <= hwdata[8*i +: 8];
                end else qa <= mem[dp_word];
            if (B_WRITE) begin : wr
                always @(posedge clk_b)
                    if (b_we) begin
                        mem[b_addr] <= b_wdata[8*i +: 8];
                        qb <= b_wdata[8*i +: 8];
                    end else qb <= mem[b_addr];
            end else begin : rd
                always @(posedge clk_b) qb <= mem[b_addr];
            end
            always @(*) begin
                hrdata[8*i +: 8] = qa;
                b_q[8*i +: 8] = qb;
            end
        end
    end
    endgenerate
endmodule
