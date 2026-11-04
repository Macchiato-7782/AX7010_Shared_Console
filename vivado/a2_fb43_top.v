`timescale 1ns / 1ps

module a2_fb43_top (
    input  wire        sys_clk,
    inout  wire [14:0] DDR_addr,
    inout  wire [2:0]  DDR_ba,
    inout  wire        DDR_cas_n,
    inout  wire        DDR_ck_n,
    inout  wire        DDR_ck_p,
    inout  wire        DDR_cke,
    inout  wire        DDR_cs_n,
    inout  wire [3:0]  DDR_dm,
    inout  wire [31:0] DDR_dq,
    inout  wire [3:0]  DDR_dqs_n,
    inout  wire [3:0]  DDR_dqs_p,
    inout  wire        DDR_odt,
    inout  wire        DDR_ras_n,
    inout  wire        DDR_reset_n,
    inout  wire        DDR_we_n,
    inout  wire        FIXED_IO_ddr_vrn,
    inout  wire        FIXED_IO_ddr_vrp,
    inout  wire [53:0] FIXED_IO_mio,
    inout  wire        FIXED_IO_ps_clk,
    inout  wire        FIXED_IO_ps_porb,
    inout  wire        FIXED_IO_ps_srstb,
    output wire        lcd_dclk,
    output reg         lcd_hs,
    output reg         lcd_vs,
    output reg         lcd_de,
    output reg  [7:0]  lcd_r,
    output reg  [7:0]  lcd_g,
    output reg  [7:0]  lcd_b,
    output wire        TMDS_clk_n,
    output wire        TMDS_clk_p,
    output wire [2:0]  TMDS_data_n,
    output wire [2:0]  TMDS_data_p,
    inout  wire        hdmi_ddc_scl_io,
    inout  wire        hdmi_ddc_sda_io,
    output wire        hdmi_oen,
    input  wire [3:0]  btns_tri_i,
    output wire [3:0]  leds_tri_o
);

    wire pclk_bufg;
    wire pclk_locked;
    wire pclk_panel;

    video_clock u_video_clk (
        .clk_in1  (sys_clk),
        .clk_out1 (pclk_bufg),
        .clk_panel(pclk_panel),
        .locked   (pclk_locked)
    );

    wire [16:0] fb_addr;
    wire [31:0] fb_dout;
    wire        boot_done;
    (* ASYNC_REG = "TRUE" *) reg [1:0] boot_done_sync;
    wire show_terminal;
    always @(posedge pclk_bufg) begin
        if (!pclk_locked) boot_done_sync <= 0;
        else boot_done_sync <= {boot_done_sync[0], boot_done};
    end
    wire [9:0]  scan_x, scan_y;
    wire        ov_en;
    wire [7:0]  ov_rgb;
    wire scan_hs, scan_vs, scan_de;
    wire [7:0] scan_r, scan_g, scan_b;

    design_1_wrapper u_sys (
        .DDR_addr          (DDR_addr),
        .DDR_ba            (DDR_ba),
        .DDR_cas_n         (DDR_cas_n),
        .DDR_ck_n          (DDR_ck_n),
        .DDR_ck_p          (DDR_ck_p),
        .DDR_cke           (DDR_cke),
        .DDR_cs_n          (DDR_cs_n),
        .DDR_dm            (DDR_dm),
        .DDR_dq            (DDR_dq),
        .DDR_dqs_n         (DDR_dqs_n),
        .DDR_dqs_p         (DDR_dqs_p),
        .DDR_odt           (DDR_odt),
        .DDR_ras_n         (DDR_ras_n),
        .DDR_reset_n       (DDR_reset_n),
        .DDR_we_n          (DDR_we_n),
        .FIXED_IO_ddr_vrn  (FIXED_IO_ddr_vrn),
        .FIXED_IO_ddr_vrp  (FIXED_IO_ddr_vrp),
        .FIXED_IO_mio      (FIXED_IO_mio),
        .FIXED_IO_ps_clk   (FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb  (FIXED_IO_ps_porb),
        .FIXED_IO_ps_srstb (FIXED_IO_ps_srstb),
        .TMDS_clk_n        (TMDS_clk_n),
        .TMDS_clk_p        (TMDS_clk_p),
        .TMDS_data_n       (TMDS_data_n),
        .TMDS_data_p       (TMDS_data_p),
        .hdmi_ddc_scl_io   (hdmi_ddc_scl_io),
        .hdmi_ddc_sda_io   (hdmi_ddc_sda_io),
        .hdmi_oen          (hdmi_oen),
        .btns_tri_i        (btns_tri_i),
        .leds_tri_o        (leds_tri_o),
        .lcd_bram_addr     (fb_addr),
        .lcd_bram_clk      (pclk_bufg),
        .lcd_bram_din      (32'd0),
        .lcd_bram_dout     (fb_dout),
        .lcd_bram_en       (1'b1),
        .lcd_bram_rst      (~pclk_locked),
        .lcd_bram_we       (1'b0),
        .boot_done         (boot_done)
    );

    lcd_fb_scanout u_scan (
        .pclk      (pclk_bufg),
        .rst       (~pclk_locked),
        .boot_done (show_terminal),
        .ov_en     (ov_en),
        .ov_rgb    (ov_rgb),
        .fb_addr   (fb_addr),
        .fb_dout   (fb_dout),
        .bg_addr   (),
        .bg_dout   (8'h00),
        .x_out     (scan_x),
        .y_out     (scan_y),
        .hs        (scan_hs),
        .vs        (scan_vs),
        .de        (scan_de),
        .r         (scan_r),
        .g         (scan_g),
        .b         (scan_b)
    );

    boot_overlay u_boot_overlay (
        .pclk   (pclk_bufg),
        .rst    (~pclk_locked),
        .x      (scan_x),
        .y      (scan_y),
        .de     (scan_de),
        .vs     (scan_vs),
        .ready  (boot_done_sync[1]),
        .handoff(show_terminal),
        .ov_en  (ov_en),
        .ov_rgb (ov_rgb)
    );

    // Register data and control together. Phase-shift DCLK so both panel
    // sampling edges land away from RGB transitions.
    always @(posedge pclk_bufg) begin
        {lcd_hs, lcd_vs, lcd_de} <= {scan_hs, scan_vs, scan_de};
        {lcd_r, lcd_g, lcd_b} <= {scan_r, scan_g, scan_b};
    end

    ODDR #(.DDR_CLK_EDGE("SAME_EDGE"), .INIT(1'b0), .SRTYPE("SYNC")) lcd_clock_out (
        .C(pclk_panel), .CE(1'b1), .D1(1'b1), .D2(1'b0),
        .Q(lcd_dclk), .R(1'b0), .S(1'b0)
    );

endmodule
