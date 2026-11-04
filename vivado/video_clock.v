`timescale 1ns / 1ps

module video_clock(
    input wire clk_in1,
    output wire clk_out1,
    output wire clk_panel,
    output wire locked
);
    wire feedback;
    wire lcd_clock;
    wire panel_clock;
    // Keep the HDMI MMCM and clock topology from the vendor design.
    // 50 MHz * 18 / 100 = 9 MHz.
    PLLE2_BASE #(
        .CLKIN1_PERIOD(20.0),
        .CLKFBOUT_MULT(18),
        .CLKOUT0_DIVIDE(100),
        .CLKOUT1_DIVIDE(100),
        .CLKOUT1_PHASE(90.0),
        .DIVCLK_DIVIDE(1),
        .STARTUP_WAIT("FALSE")
    ) pll (
        .CLKIN1(clk_in1),
        .CLKFBIN(feedback),
        .CLKFBOUT(feedback),
        .CLKOUT0(lcd_clock),
        .CLKOUT1(panel_clock),
        .LOCKED(locked),
        .PWRDWN(1'b0),
        .RST(1'b0)
    );
    BUFG lcd_buf (.I(lcd_clock), .O(clk_out1));
    BUFG panel_buf (.I(panel_clock), .O(clk_panel));
endmodule
