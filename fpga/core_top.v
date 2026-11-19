//
// SkyRoads core top level, instantiated by the APF's apf_top.  The port list
// is the openFPGA core template's (vendor/core-template, core/core_top.v).
//

`default_nettype none

module core_top (

//
// physical connections
//

///////////////////////////////////////////////////
// clock inputs 74.25mhz. not phase aligned, so treat these domains as asynchronous

input   wire            clk_74a, // mainclk1
input   wire            clk_74b, // mainclk1 

///////////////////////////////////////////////////
// cartridge interface
// switches between 3.3v and 5v mechanically
// output enable for multibit translators controlled by pic32

// GBA AD[15:8]
inout   wire    [7:0]   cart_tran_bank2,
output  wire            cart_tran_bank2_dir,

// GBA AD[7:0]
inout   wire    [7:0]   cart_tran_bank3,
output  wire            cart_tran_bank3_dir,

// GBA A[23:16]
inout   wire    [7:0]   cart_tran_bank1,
output  wire            cart_tran_bank1_dir,

// GBA [7] PHI#
// GBA [6] WR#
// GBA [5] RD#
// GBA [4] CS1#/CS#
//     [3:0] unwired
inout   wire    [7:4]   cart_tran_bank0,
output  wire            cart_tran_bank0_dir,

// GBA CS2#/RES#
inout   wire            cart_tran_pin30,
output  wire            cart_tran_pin30_dir,
// when GBC cart is inserted, this signal when low or weak will pull GBC /RES low with a special circuit
// the goal is that when unconfigured, the FPGA weak pullups won't interfere.
// thus, if GBC cart is inserted, FPGA must drive this high in order to let the level translators
// and general IO drive this pin.
output  wire            cart_pin30_pwroff_reset,

// GBA IRQ/DRQ
inout   wire            cart_tran_pin31,
output  wire            cart_tran_pin31_dir,

// infrared
input   wire            port_ir_rx,
output  wire            port_ir_tx,
output  wire            port_ir_rx_disable, 

// GBA link port
inout   wire            port_tran_si,
output  wire            port_tran_si_dir,
inout   wire            port_tran_so,
output  wire            port_tran_so_dir,
inout   wire            port_tran_sck,
output  wire            port_tran_sck_dir,
inout   wire            port_tran_sd,
output  wire            port_tran_sd_dir,
 
///////////////////////////////////////////////////
// cellular psram 0 and 1, two chips (64mbit x2 dual die per chip)

output  wire    [21:16] cram0_a,
inout   wire    [15:0]  cram0_dq,
input   wire            cram0_wait,
output  wire            cram0_clk,
output  wire            cram0_adv_n,
output  wire            cram0_cre,
output  wire            cram0_ce0_n,
output  wire            cram0_ce1_n,
output  wire            cram0_oe_n,
output  wire            cram0_we_n,
output  wire            cram0_ub_n,
output  wire            cram0_lb_n,

output  wire    [21:16] cram1_a,
inout   wire    [15:0]  cram1_dq,
input   wire            cram1_wait,
output  wire            cram1_clk,
output  wire            cram1_adv_n,
output  wire            cram1_cre,
output  wire            cram1_ce0_n,
output  wire            cram1_ce1_n,
output  wire            cram1_oe_n,
output  wire            cram1_we_n,
output  wire            cram1_ub_n,
output  wire            cram1_lb_n,

///////////////////////////////////////////////////
// sdram, 512mbit 16bit

output  wire    [12:0]  dram_a,
output  wire    [1:0]   dram_ba,
inout   wire    [15:0]  dram_dq,
output  wire    [1:0]   dram_dqm,
output  wire            dram_clk,
output  wire            dram_cke,
output  wire            dram_ras_n,
output  wire            dram_cas_n,
output  wire            dram_we_n,

///////////////////////////////////////////////////
// sram, 1mbit 16bit

output  wire    [16:0]  sram_a,
inout   wire    [15:0]  sram_dq,
output  wire            sram_oe_n,
output  wire            sram_we_n,
output  wire            sram_ub_n,
output  wire            sram_lb_n,

///////////////////////////////////////////////////
// vblank driven by dock for sync in a certain mode

input   wire            vblank,

///////////////////////////////////////////////////
// i/o to 6515D breakout usb uart

output  wire            dbg_tx,
input   wire            dbg_rx,

///////////////////////////////////////////////////
// i/o pads near jtag connector user can solder to

output  wire            user1,
input   wire            user2,

///////////////////////////////////////////////////
// RFU internal i2c bus 

inout   wire            aux_sda,
output  wire            aux_scl,

///////////////////////////////////////////////////
// RFU, do not use
output  wire            vpll_feed,


//
// logical connections
//

///////////////////////////////////////////////////
// video, audio output to scaler
output  wire    [23:0]  video_rgb,
output  wire            video_rgb_clock,
output  wire            video_rgb_clock_90,
output  wire            video_de,
output  wire            video_skip,
output  wire            video_vs,
output  wire            video_hs,
    
output  wire            audio_mclk,
input   wire            audio_adc,
output  wire            audio_dac,
output  wire            audio_lrck,

///////////////////////////////////////////////////
// bridge bus connection
// synchronous to clk_74a
output  wire            bridge_endian_little,
input   wire    [31:0]  bridge_addr,
input   wire            bridge_rd,
output  reg     [31:0]  bridge_rd_data,
input   wire            bridge_wr,
input   wire    [31:0]  bridge_wr_data,

///////////////////////////////////////////////////
// controller data
// 
// key bitmap:
//   [0]    dpad_up
//   [1]    dpad_down
//   [2]    dpad_left
//   [3]    dpad_right
//   [4]    face_a
//   [5]    face_b
//   [6]    face_x
//   [7]    face_y
//   [8]    trig_l1
//   [9]    trig_r1
//   [10]   trig_l2
//   [11]   trig_r2
//   [12]   trig_l3
//   [13]   trig_r3
//   [14]   face_select
//   [15]   face_start
//   [31:28] type
// joy values - unsigned
//   [ 7: 0] lstick_x
//   [15: 8] lstick_y
//   [23:16] rstick_x
//   [31:24] rstick_y
// trigger values - unsigned
//   [ 7: 0] ltrig
//   [15: 8] rtrig
//
input   wire    [31:0]  cont1_key,
input   wire    [31:0]  cont2_key,
input   wire    [31:0]  cont3_key,
input   wire    [31:0]  cont4_key,
input   wire    [31:0]  cont1_joy,
input   wire    [31:0]  cont2_joy,
input   wire    [31:0]  cont3_joy,
input   wire    [31:0]  cont4_joy,
input   wire    [15:0]  cont1_trig,
input   wire    [15:0]  cont2_trig,
input   wire    [15:0]  cont3_trig,
input   wire    [15:0]  cont4_trig
    
);

// ---------------------------------------------------------------------------
// SkyRoads: the sky_soc (../rtl) wired to the Pocket.
//
// Data slots (dist/.../data.json), written over the bridge before the core
// leaves reset:
//   0  skyroads_fw.bin    bridge 0x00000000 -> SDRAM 0x0000000 (CPU 0x10000000)
//   1  skyroads_data.bin  bridge 0x00800000 -> SDRAM 0x0800000 (CPU 0x10800000)
//   2  skyroads.sav       bridge 0x20000000 -> save RAM (nonvolatile, 1 KiB)
// The CPU runs once the host has left reset, all slots are loaded and the
// last loader write has reached the SDRAM.
// ---------------------------------------------------------------------------

localparam SYS_HZ = 49152000;         // 4 x 12.288 MHz: one PLL for both

assign port_ir_tx = 0;
assign port_ir_rx_disable = 1;
assign bridge_endian_little = 0;        // bridge words are big endian

// cartridge, link port, cellular RAM and SRAM unused (as in the template)
assign cart_tran_bank3 = 8'hzz;
assign cart_tran_bank3_dir = 1'b0;
assign cart_tran_bank2 = 8'hzz;
assign cart_tran_bank2_dir = 1'b0;
assign cart_tran_bank1 = 8'hzz;
assign cart_tran_bank1_dir = 1'b0;
assign cart_tran_bank0 = 4'hf;
assign cart_tran_bank0_dir = 1'b1;
assign cart_tran_pin30 = 1'b0;
assign cart_tran_pin30_dir = 1'bz;
assign cart_pin30_pwroff_reset = 1'b0;
assign cart_tran_pin31 = 1'bz;
assign cart_tran_pin31_dir = 1'b0;
assign port_tran_so = 1'bz;
assign port_tran_so_dir = 1'b0;
assign port_tran_si = 1'bz;
assign port_tran_si_dir = 1'b0;
assign port_tran_sck = 1'bz;
assign port_tran_sck_dir = 1'b0;
assign port_tran_sd = 1'bz;
assign port_tran_sd_dir = 1'b0;

assign cram0_a = 'h0;
assign cram0_dq = {16{1'bZ}};
assign cram0_clk = 0;
assign cram0_adv_n = 1;
assign cram0_cre = 0;
assign cram0_ce0_n = 1;
assign cram0_ce1_n = 1;
assign cram0_oe_n = 1;
assign cram0_we_n = 1;
assign cram0_ub_n = 1;
assign cram0_lb_n = 1;
assign cram1_a = 'h0;
assign cram1_dq = {16{1'bZ}};
assign cram1_clk = 0;
assign cram1_adv_n = 1;
assign cram1_cre = 0;
assign cram1_ce0_n = 1;
assign cram1_ce1_n = 1;
assign cram1_oe_n = 1;
assign cram1_we_n = 1;
assign cram1_ub_n = 1;
assign cram1_lb_n = 1;

assign sram_a = 'h0;
assign sram_dq = {16{1'bZ}};
assign sram_oe_n  = 1;
assign sram_we_n  = 1;
assign sram_ub_n  = 1;
assign sram_lb_n  = 1;

assign dbg_tx = 1'bZ;
assign user1 = 1'bZ;
assign aux_scl = 1'bZ;
assign vpll_feed = 1'bZ;

// ---------------------------------------------------------------------------
// clocks: 12.288 MHz video (+90 degrees) and 49.152 MHz system; the SDRAM's
// clock is the system clock inverted (sdram_clk_pin below)
// ---------------------------------------------------------------------------
wire clk_vid, clk_vid_90, clk_sys;
wire pll_core_locked, pll_core_locked_s;
synch_3 s01(pll_core_locked, pll_core_locked_s, clk_74a);

mf_pllbase mp1 (
    .refclk   ( clk_74a ),
    .rst      ( 0 ),
    .outclk_0 ( clk_vid ),
    .outclk_1 ( clk_vid_90 ),
    .outclk_2 ( clk_sys ),
    .outclk_3 ( ),
    .outclk_4 ( ),
    .locked   ( pll_core_locked )
);

// system reset: the PLL is locked
reg [3:0] sys_rst_cnt = 0;
wire      pll_locked_sys;
synch_3 s02(pll_core_locked, pll_locked_sys, clk_sys);
always @(posedge clk_sys)
    if (!pll_locked_sys) sys_rst_cnt <= 0;
    else if (sys_rst_cnt != 4'hf) sys_rst_cnt <= sys_rst_cnt + 1'b1;
wire sys_rst_n = sys_rst_cnt == 4'hf;

wire vid_rst_n;
synch_3 s03(pll_core_locked, vid_rst_n, clk_vid);

// ---------------------------------------------------------------------------
// host commands (the template's bridge command handler)
// ---------------------------------------------------------------------------
wire            reset_n;
wire    [31:0]  cmd_bridge_rd_data;
wire            status_boot_done = pll_core_locked_s;
wire            status_setup_done = pll_core_locked_s;
wire            status_running = reset_n;

wire            dataslot_requestread;
wire    [15:0]  dataslot_requestread_id;
wire            dataslot_requestread_ack = 1;
wire            dataslot_requestread_ok = 1;
wire            dataslot_requestwrite;
wire    [15:0]  dataslot_requestwrite_id;
wire    [31:0]  dataslot_requestwrite_size;
wire            dataslot_requestwrite_ack = 1;
wire            dataslot_requestwrite_ok = 1;
wire            dataslot_update;
wire    [15:0]  dataslot_update_id;
wire    [31:0]  dataslot_update_size;
wire            dataslot_allcomplete;
wire    [31:0]  rtc_epoch_seconds, rtc_date_bcd, rtc_time_bcd;
wire            rtc_valid;
// save states (sleep): see the bridge's save-state section below
wire            savestate_supported = 1;
wire    [31:0]  savestate_addr = 32'h40000000;
reg     [31:0]  savestate_size;
wire    [31:0]  savestate_maxloadsize = 32'h01000000;
wire            savestate_start, savestate_load;
reg             savestate_start_ack = 0, savestate_start_busy = 0, savestate_start_ok = 0, savestate_start_err = 0;
reg             savestate_load_ack = 0, savestate_load_busy = 0, savestate_load_ok = 0, savestate_load_err = 0;
wire            osnotify_inmenu;
reg             target_dataslot_read = 0, target_dataslot_write = 0;
reg             target_dataslot_getfile = 0, target_dataslot_openfile = 0;
wire            target_dataslot_ack, target_dataslot_done;
wire    [2:0]   target_dataslot_err;
reg     [15:0]  target_dataslot_id = 0;
reg     [31:0]  target_dataslot_slotoffset = 0, target_dataslot_bridgeaddr = 0, target_dataslot_length = 0;
wire    [31:0]  target_buffer_param_struct = 0, target_buffer_resp_struct = 0;
reg     [9:0]   datatable_addr = 0;
reg             datatable_wren = 0;
reg     [31:0]  datatable_data = 0;
wire    [31:0]  datatable_q;

core_bridge_cmd icb (
    .clk                        ( clk_74a ),
    .reset_n                    ( reset_n ),
    .bridge_endian_little       ( bridge_endian_little ),
    .bridge_addr                ( bridge_addr ),
    .bridge_rd                  ( bridge_rd ),
    .bridge_rd_data             ( cmd_bridge_rd_data ),
    .bridge_wr                  ( bridge_wr ),
    .bridge_wr_data             ( bridge_wr_data ),
    .status_boot_done           ( status_boot_done ),
    .status_setup_done          ( status_setup_done ),
    .status_running             ( status_running ),
    .dataslot_requestread       ( dataslot_requestread ),
    .dataslot_requestread_id    ( dataslot_requestread_id ),
    .dataslot_requestread_ack   ( dataslot_requestread_ack ),
    .dataslot_requestread_ok    ( dataslot_requestread_ok ),
    .dataslot_requestwrite      ( dataslot_requestwrite ),
    .dataslot_requestwrite_id   ( dataslot_requestwrite_id ),
    .dataslot_requestwrite_size ( dataslot_requestwrite_size ),
    .dataslot_requestwrite_ack  ( dataslot_requestwrite_ack ),
    .dataslot_requestwrite_ok   ( dataslot_requestwrite_ok ),
    .dataslot_update            ( dataslot_update ),
    .dataslot_update_id         ( dataslot_update_id ),
    .dataslot_update_size       ( dataslot_update_size ),
    .dataslot_allcomplete       ( dataslot_allcomplete ),
    .rtc_epoch_seconds          ( rtc_epoch_seconds ),
    .rtc_date_bcd               ( rtc_date_bcd ),
    .rtc_time_bcd               ( rtc_time_bcd ),
    .rtc_valid                  ( rtc_valid ),
    .savestate_supported        ( savestate_supported ),
    .savestate_addr             ( savestate_addr ),
    .savestate_size             ( savestate_size ),
    .savestate_maxloadsize      ( savestate_maxloadsize ),
    .savestate_start            ( savestate_start ),
    .savestate_start_ack        ( savestate_start_ack ),
    .savestate_start_busy       ( savestate_start_busy ),
    .savestate_start_ok         ( savestate_start_ok ),
    .savestate_start_err        ( savestate_start_err ),
    .savestate_load             ( savestate_load ),
    .savestate_load_ack         ( savestate_load_ack ),
    .savestate_load_busy        ( savestate_load_busy ),
    .savestate_load_ok          ( savestate_load_ok ),
    .savestate_load_err         ( savestate_load_err ),
    .osnotify_inmenu            ( osnotify_inmenu ),
    .target_dataslot_read       ( target_dataslot_read ),
    .target_dataslot_write      ( target_dataslot_write ),
    .target_dataslot_getfile    ( target_dataslot_getfile ),
    .target_dataslot_openfile   ( target_dataslot_openfile ),
    .target_dataslot_ack        ( target_dataslot_ack ),
    .target_dataslot_done       ( target_dataslot_done ),
    .target_dataslot_err        ( target_dataslot_err ),
    .target_dataslot_id         ( target_dataslot_id ),
    .target_dataslot_slotoffset ( target_dataslot_slotoffset ),
    .target_dataslot_bridgeaddr ( target_dataslot_bridgeaddr ),
    .target_dataslot_length     ( target_dataslot_length ),
    .target_buffer_param_struct ( target_buffer_param_struct ),
    .target_buffer_resp_struct  ( target_buffer_resp_struct ),
    .datatable_addr             ( datatable_addr ),
    .datatable_wren             ( datatable_wren ),
    .datatable_data             ( datatable_data ),
    .datatable_q                ( datatable_q )
);

// the data slot table (Dataslot ID/Size, two words per slot in data.json's
// order): the Pocket writes back as many bytes of a nonvolatile slot as the
// table says when the core is shut down (quit, power off, sleep).  So the
// save slot (the third: words 4 and 5) says 1 KiB, written every cycle as
// the openFPGA cores by agg23 do, since the Pocket also writes the table
// (when it loads the slots: 0 bytes when there was no save file yet)
always @(posedge clk_74a) begin
    datatable_addr <= 10'd5;
    datatable_data <= 32'd1024;
    datatable_wren <= 1'b1;
end

// ---------------------------------------------------------------------------
// bridge: data slot writes into the SDRAM, save RAM reads and writes
// ---------------------------------------------------------------------------
function [31:0] swap32(input [31:0] v);
    swap32 = {v[7:0], v[15:8], v[23:16], v[31:24]};
endfunction

wire bridge_sdram = bridge_addr[31:26] == 6'b000000;      // 0x00000000-0x03ffffff
wire bridge_save  = bridge_addr[31:10] == 22'h080000;     // 0x20000000-0x200003ff
wire bridge_stage = bridge_addr[31:25] == 7'b0100000;     // 0x40000000-0x41ffffff: save states

// Core Settings (interact.json): the Pocket writes them at 0x60000000; the
// firmware reads them (SETTINGS register).  Bit 0: the music player
reg  [31:0] core_settings = 0;
always @(posedge clk_74a)
    if (bridge_wr && bridge_addr == 32'h60000000) core_settings <= bridge_wr_data;

// loader: bridge words cross into the system clock in a small FIFO; a save
// state written to 0x40000000 goes to the SDRAM's staging area (+32 MiB)
wire        ldq_full, ldq_empty, ld_ready;
wire [55:0] ldq_out;
async_fifo #(.W(56), .DEPTH_LOG2(4)) ldq (
    .wclk(clk_74a), .wrst_n(pll_core_locked_s),
    .we(bridge_wr && (bridge_sdram || bridge_stage)),
    .wdata({bridge_stage ? {1'b1, bridge_addr[24:2]} : bridge_addr[25:2], swap32(bridge_wr_data)}),
    .full(ldq_full),
    .rclk(clk_sys), .rrst_n(sys_rst_n),
    .re(ld_ready), .rdata(ldq_out), .empty(ldq_empty));

// ---------------------------------------------------------------------------
// save states, the bridge's side (clk_74a).  [00A0 Savestate: Start]: the
// firmware is asked (through the SoC) and copies the machine into the
// staging area; when it says done, the Pocket reads ss_size bytes from
// 0x40000000, each word due by the next read strobe (the SoC's reader
// fetches ahead).  [00A4 Savestate: Load]: the Pocket has already written
// the blob to 0x40000000; once those writes are in the SDRAM the firmware
// is told, at once if it runs, or when it starts.
// ---------------------------------------------------------------------------
wire        ss_save_done, ss_save_fail;
wire [31:0] ss_size, ss_rd_data;
reg         ss_save_tog = 0, ss_load_tog = 0, ss_rd_tog = 0;
reg  [22:0] ss_rd_addr_74;
reg         ss_rd_last_v = 0;
reg         bridge_rd_74 = 0, ss_start_74 = 0, ss_load_74 = 0;
reg  [27:0] ss_timeout = 0;
wire        ss_done_74, ss_fail_74;
always @(posedge clk_74a) begin
    bridge_rd_74 <= bridge_rd;
    ss_start_74 <= savestate_start;
    ss_load_74 <= savestate_load;
    savestate_start_ack <= savestate_start;
    savestate_load_ack <= savestate_load;
    savestate_size <= ss_size;          // quasi-static: set before the done pulse

    if (savestate_start && !ss_start_74) begin
        savestate_start_busy <= 1'b1; savestate_start_ok <= 1'b0; savestate_start_err <= 1'b0;
        savestate_load_ok <= 1'b0; savestate_load_err <= 1'b0;
        ss_save_tog <= !ss_save_tog;
        ss_timeout <= 28'd222750000;    // 3 s
        ss_rd_last_v <= 1'b0;
    end else if (savestate_start_busy) begin
        if (ss_done_74) begin savestate_start_busy <= 1'b0; savestate_start_ok <= 1'b1; ss_rd_last_v <= 1'b0; end
        else if (ss_fail_74 || ss_timeout == 0) begin savestate_start_busy <= 1'b0; savestate_start_err <= 1'b1; end
        else ss_timeout <= ss_timeout - 1'b1;
    end

    if (savestate_load && !ss_load_74) begin
        // the blob is already written: the load itself happens in the firmware
        savestate_load_ok <= 1'b1; savestate_load_err <= 1'b0;
        savestate_start_ok <= 1'b0; savestate_start_err <= 1'b0;
        ss_load_tog <= !ss_load_tog;
    end

    // a read of the staging area: the word goes to the SoC's reader (a
    // repeated read of the same address keeps the word it has)
    if (bridge_rd && !bridge_rd_74 && bridge_stage &&
        !(ss_rd_last_v && ss_rd_addr_74 == bridge_addr[24:2])) begin
        ss_rd_addr_74 <= bridge_addr[24:2];
        ss_rd_last_v <= 1'b1;
        ss_rd_tog <= !ss_rd_tog;
    end
end

// toggles across: requests into the system clock, answers back
reg [2:0] ss_save_s = 0, ss_load_s = 0, ss_rd_s = 0;
reg       ss_load_wait = 0;
reg       ss_done_t = 0, ss_fail_t = 0;
always @(posedge clk_sys) begin
    ss_save_s <= {ss_save_s[1:0], ss_save_tog};
    ss_load_s <= {ss_load_s[1:0], ss_load_tog};
    ss_rd_s <= {ss_rd_s[1:0], ss_rd_tog};
    if (ss_load_s[2] != ss_load_s[1]) ss_load_wait <= 1'b1;
    else if (ss_load_wait && ldq_empty) ss_load_wait <= 1'b0;
    if (ss_save_done) ss_done_t <= !ss_done_t;
    if (ss_save_fail) ss_fail_t <= !ss_fail_t;
end
wire ss_save_req_sys = ss_save_s[2] != ss_save_s[1];
wire ss_load_req_sys = ss_load_wait && ldq_empty;       // the blob's writes are in
wire ss_rd_req_sys   = ss_rd_s[2] != ss_rd_s[1];
reg [2:0] ss_done_s = 0, ss_fail_s = 0;
always @(posedge clk_74a) begin
    ss_done_s <= {ss_done_s[1:0], ss_done_t};
    ss_fail_s <= {ss_fail_s[1:0], ss_fail_t};
end
assign ss_done_74 = ss_done_s[2] != ss_done_s[1];
assign ss_fail_74 = ss_fail_s[2] != ss_fail_s[1];

// the save RAM's word for a read is taken at its strobe and answered at the
// next read, as the Pocket expects (it sets up the next address before it
// takes the data; the last address is read twice, or once more past the
// end, hence the window of 2 KiB)
wire [31:0] save_q;
reg  [31:0] save_rd_q = 0;
always @(posedge clk_74a)
    if (bridge_rd && !bridge_rd_74 && bridge_save) save_rd_q <= swap32(save_q);
always @(*) begin
    casex (bridge_addr)
        32'b0010_0000_0000_0000_0000_0xxx_xxxx_xxxx: bridge_rd_data = save_rd_q;
        32'b0100_000x_xxxx_xxxx_xxxx_xxxx_xxxx_xxxx: bridge_rd_data = swap32(ss_rd_data);  // quasi-static
        32'hF8xxxxxx: bridge_rd_data = cmd_bridge_rd_data;
        default: bridge_rd_data = 0;
    endcase
end

// ---------------------------------------------------------------------------
// run control: the CPU starts once the slots are loaded and in the SDRAM,
// and keeps running while the bridge writes (a save state being loaded)
// ---------------------------------------------------------------------------
wire allcomplete_sys, reset_n_sys;
synch_3 s04(dataslot_allcomplete, allcomplete_sys, clk_sys);
synch_3 s05(reset_n, reset_n_sys, clk_sys);
wire sdram_ready;
reg  cpu_run = 0;
always @(posedge clk_sys)
    if (!reset_n_sys) cpu_run <= 1'b0;
    else if (allcomplete_sys && ldq_empty && sdram_ready) cpu_run <= 1'b1;

wire [31:0] keys_sys;
synch_3 #(.WIDTH(32)) s06(cont1_key, keys_sys, clk_sys);

// ---------------------------------------------------------------------------
// the SoC
// ---------------------------------------------------------------------------
wire        sd_dq_oe;
wire [15:0] sd_dq_out;
// read data: caught at the pad on the falling edge of the system clock, which
// is about when the chip's clock rises (it is the inverted system clock,
// delayed by the pads and the board), and so the middle of the window in
// which the chip drives a word (from tAC after its previous edge to tOH
// after this one); then into the system clock's rising edge.  A rising-edge
// capture half a cycle later falls after tOH, between two words.
reg  [15:0] sd_dq_neg, sd_dq_in;
always @(negedge clk_sys) sd_dq_neg <= dram_dq;
always @(posedge clk_sys) sd_dq_in <= sd_dq_neg;
assign dram_dq = sd_dq_oe ? sd_dq_out : 16'hzzzz;

wire signed [15:0] sample;
wire [23:0] vid_rgb;
wire        vid_de, vid_hs, vid_vs;

sky_soc #(.SYS_HZ(SYS_HZ)) soc (
    .clk(clk_sys), .rst_n(sys_rst_n), .cpu_run(cpu_run),
    .ss_save_req(ss_save_req_sys), .ss_load_req(ss_load_req_sys),
    .ss_save_done(ss_save_done), .ss_save_fail(ss_save_fail), .ss_size(ss_size),
    .ss_rd_req(ss_rd_req_sys), .ss_rd_addr(ss_rd_addr_74), .ss_rd_data(ss_rd_data),
    .settings(core_settings),           // quasi-static
    .keys(keys_sys),
    .dbg_status({pll_locked_sys, reset_n_sys, allcomplete_sys, ldq_empty}),
    .ld_valid(!ldq_empty), .ld_addr(ldq_out[55:32]), .ld_data(ldq_out[31:0]),
    .ld_ready(ld_ready), .sdram_ready(sdram_ready),
    .clk_b(clk_74a), .save_addr(bridge_addr[9:2]), .save_we(bridge_wr && bridge_save),
    .save_wdata(swap32(bridge_wr_data)), .save_q(save_q),
    .clk_vid(clk_vid), .vid_rst_n(vid_rst_n),
    .vid_rgb(vid_rgb), .vid_de(vid_de), .vid_hs(vid_hs), .vid_vs(vid_vs),
    .sample(sample),
    .sd_cke(dram_cke), .sd_ba(dram_ba), .sd_a(dram_a), .sd_ras_n(dram_ras_n),
    .sd_cas_n(dram_cas_n), .sd_we_n(dram_we_n), .sd_dqm(dram_dqm),
    .sd_dq_out(sd_dq_out), .sd_dq_oe(sd_dq_oe), .sd_dq_in(sd_dq_in),
    .dbg_putc(), .dbg_char(), .sim_exit(), .sim_code());

// the SDRAM's clock: the system clock inverted, through a DDIO output (low
// in the first half of each cycle, high in the second): the chip takes the
// commands and write data half a cycle after they are registered, and its
// read data reaches the pad register CL cycles later (sdram_ctrl RD_LAT)
pin_ddio_clk sdram_clk_pin (
    .datain_h ( 1'b0 ),
    .datain_l ( 1'b1 ),
    .outclock ( clk_sys ),
    .dataout  ( dram_clk )
);

// ---------------------------------------------------------------------------
// video
// ---------------------------------------------------------------------------
assign video_rgb_clock = clk_vid;
assign video_rgb_clock_90 = clk_vid_90;
assign video_rgb = vid_rgb;
assign video_de = vid_de;
assign video_skip = 1'b0;
assign video_vs = vid_vs;
assign video_hs = vid_hs;

// ---------------------------------------------------------------------------
// audio: I2S, 48 kHz, 16 bits in 32-bit slots (the template's generator)
// ---------------------------------------------------------------------------
assign audio_mclk = audgen_mclk;
assign audio_dac = audgen_dac;
assign audio_lrck = audgen_lrck;

reg         [21:0]  audgen_accum = 0;
reg                 audgen_mclk = 0;
parameter   [20:0]  CYCLE_48KHZ = 21'd122880 * 2;
always @(posedge clk_74a) begin
    audgen_accum <= audgen_accum + CYCLE_48KHZ;
    if (audgen_accum >= 21'd742500) begin
        audgen_mclk <= ~audgen_mclk;
        audgen_accum <= audgen_accum - 21'd742500 + CYCLE_48KHZ;
    end
end

reg [1:0] aud_mclk_divider = 0;
wire      audgen_sclk = aud_mclk_divider[1] /* synthesis keep */;
always @(posedge audgen_mclk) aud_mclk_divider <= aud_mclk_divider + 1'b1;

// the sample crosses from the system clock: the I2S side toggles a request
// once per frame; the system side answers by holding the next sample in
// aud_hold, which is stable for the ~1000 cycles until it is read
reg        aud_req = 0;
reg [2:0]  aud_req_s = 0;
reg [15:0] aud_hold = 0;
always @(posedge clk_sys) begin
    aud_req_s <= {aud_req_s[1:0], aud_req};
    if (aud_req_s[2] != aud_req_s[1]) aud_hold <= sample;
end

reg [4:0]  audgen_lrck_cnt = 0;
reg        audgen_lrck = 0;
reg        audgen_dac = 0;
reg [31:0] aud_shift = 0;
always @(negedge audgen_sclk) begin
    audgen_lrck_cnt <= audgen_lrck_cnt + 1'b1;
    audgen_dac <= aud_shift[31];
    aud_shift <= {aud_shift[30:0], 1'b0};
    if (audgen_lrck_cnt == 31) begin
        audgen_lrck <= ~audgen_lrck;
        // I2S: the MSB one bit clock after the LRCK edge; same sample both sides
        aud_shift <= {aud_hold, 16'h0};
        if (audgen_lrck) aud_req <= ~aud_req;
    end
end

endmodule
`default_nettype wire
