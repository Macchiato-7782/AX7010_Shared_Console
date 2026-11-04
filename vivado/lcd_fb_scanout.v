`timescale 1ns / 1ps

module lcd_fb_scanout (
    input  wire        pclk,
    input  wire        rst,
    input  wire        boot_done,
    input  wire        ov_en,
    input  wire [7:0]  ov_rgb,
    output wire [16:0] fb_addr,
    input  wire [31:0] fb_dout,
    output wire [14:0] bg_addr,
    input  wire [7:0]  bg_dout,
    output wire [9:0]  x_out,
    output wire [9:0]  y_out,
    output wire        hs,
    output wire        vs,
    output wire        de,
    output wire [7:0]  r,
    output wire [7:0]  g,
    output wire [7:0]  b
);

    localparam H_ACTIVE = 480;
    localparam H_FP     = 2;
    localparam H_SYNC   = 41;
    localparam H_BP     = 2;
    localparam H_TOTAL  = H_ACTIVE + H_FP + H_SYNC + H_BP;
    localparam V_ACTIVE = 272;
    localparam V_FP     = 2;
    localparam V_SYNC   = 10;
    localparam V_BP     = 2;
    localparam V_TOTAL  = V_ACTIVE + V_FP + V_SYNC + V_BP;
    localparam H_START  = H_FP + H_SYNC + H_BP;
    localparam V_START  = V_FP + V_SYNC + V_BP;

    reg [9:0] hc = 10'd0;
    reg [9:0] vc = 10'd0;
    always @(posedge pclk) begin
        if (rst) begin
            hc <= 10'd0;
            vc <= 10'd0;
        end else if (hc == H_TOTAL - 1) begin
            hc <= 10'd0;
            if (vc == V_TOTAL - 1)
                vc <= 10'd0;
            else
                vc <= vc + 10'd1;
        end else begin
            hc <= hc + 10'd1;
        end
    end

    wire h_active = (hc >= H_START) && (hc < H_START + H_ACTIVE);
    wire v_active = (vc >= V_START) && (vc < V_START + V_ACTIVE);
    wire de_i     = h_active && v_active;

    wire [9:0] ax = hc - H_START;
    wire [9:0] ay = vc - V_START;
    wire [6:0] word_x = ax[9:2];

    wire [16:0] word_addr = (v_active ? ay : 10'd0) * 16'd120 + (h_active ? word_x : 7'd0);
    assign fb_addr = word_addr << 2;

    wire [15:0] bg_linear = (v_active ? {1'b0, ay[9:1]} : 10'd0) * 16'd240 +
                            (h_active ? {1'b0, ax[9:1]} : 10'd0);
    assign bg_addr = bg_linear[14:0];

    wire hs_i = ~((hc >= H_FP) && (hc < H_FP + H_SYNC));
    wire vs_i = ~((vc >= V_FP) && (vc < V_FP + V_SYNC));

    reg [31:0] data_r;
    reg [1:0]  byte_r;
    reg        de_r;
    reg        hs_d1, hs_d2;
    reg        vs_d1, vs_d2;
    reg [9:0]  ax_d1, ay_d1;
    reg        de_d1;
    always @(posedge pclk) begin
        ax_d1   <= ax;
        ay_d1   <= ay;
        de_d1   <= de_i;
        data_r  <= fb_dout;
        byte_r  <= ax_d1[1:0];
        de_r    <= de_d1;
        hs_d1   <= hs_i;
        hs_d2   <= hs_d1;
        vs_d1   <= vs_i;
        vs_d2   <= vs_d1;
    end

    wire [7:0] pix = data_r[{byte_r, 3'b000} +: 8];
    wire [7:0] base_pix = boot_done ? pix : bg_dout;
    wire [7:0] pix_out = (!boot_done && ov_en) ? ov_rgb : base_pix;

    assign x_out = ax_d1;
    assign y_out = ay_d1;
    assign hs = hs_d2;
    assign vs = vs_d2;
    assign de = de_r;
    // Keep RGB332 for general ANSI colors. Reserve selected values for the UI.
    reg [23:0] rgb888;
    always @* begin
        case (pix_out)
            8'h24: rgb888 = 24'h202225; // terminal background
            8'h25: rgb888 = 24'h101719; // boot background
            8'h26: rgb888 = 24'h385455; // active traces
            8'h27: rgb888 = 24'h1A292C; // inactive traces
            8'h28: rgb888 = 24'h829DA0; // lower logo band and pads
            8'h29: rgb888 = 24'hA6B8B8; // boot captions
            8'h2A: rgb888 = 24'h292D31; // terminal title bar
            8'h2B: rgb888 = 24'h424A51; // title divider
            8'h2C: rgb888 = 24'hA0A9B1; // secondary terminal text
            8'hDB: rgb888 = 24'hD4D4D4; // normal foreground
            8'h7F: rgb888 = 24'h4EC9B0; // user and host
            8'h73: rgb888 = 24'h569CD6; // working directory
            8'hED: rgb888 = 24'hF48771; // errors
            default: rgb888 = {pix_out[7:5], pix_out[7:5], pix_out[7:6],
                               pix_out[4:2], pix_out[4:2], pix_out[4:3],
                               {4{pix_out[1:0]}}};
        endcase
    end

    assign {r, g, b} = de_r ? rgb888 : 24'h000000;

endmodule
