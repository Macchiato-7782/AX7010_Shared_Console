`timescale 1ns / 1ps

module boot_overlay (
    input wire pclk, rst,
    input wire [9:0] x, y,
    input wire de, vs, ready,
    output reg handoff,
    output reg ov_en,
    output reg [7:0] ov_rgb
);
    reg [10:0] frames;
    reg [6:0] percent;
    reg [1:0] tick;
    reg [3:0] finish_frames;
    reg vs_prev;

    always @(posedge pclk) begin
        if (rst) begin
            frames <= 0;
            percent <= 0;
            tick <= 0;
            finish_frames <= 0;
            handoff <= 0;
            vs_prev <= 0;
        end else begin
            vs_prev <= vs;
            if (vs && !vs_prev) begin
                if (frames < 1800) frames <= frames + 1'b1;
                if (ready) begin
                    percent <= 100;
                    if (finish_frames < 12) finish_frames <= finish_frames + 1'b1;
                    else handoff <= 1;
                end else if (tick == 2) begin
                    tick <= 0;
                    if (percent < 95) percent <= percent + 1'b1;
                end else tick <= tick + 1'b1;
            end
        end
    end

    wire timed_out = (frames == 1800 && percent != 100);
    wire [9:0] number_left = (percent == 100) ? 192 : (percent >= 10 ? 204 : 216);
    wire [9:0] number_x = x - number_left;
    wire in_number = x >= number_left && x < 480-number_left && y >= 115 && y < 163;
    wire [2:0] number_cell = number_x / 24;
    wire [4:0] number_offset = number_x % 24;
    reg [6:0] digit;
    always @* begin
        if (percent == 100) begin
            case (number_cell)
                0: digit = "1";
                1, 2: digit = "0";
                default: digit = "%";
            endcase
        end else if (number_cell == 0)
            digit = "0" + (percent >= 10 ? percent / 10 : percent);
        else if (number_cell == 1 && percent >= 10)
            digit = "0" + percent % 10;
        else digit = "%";
    end

    localparam [143:0] START = "Starting Linux    ";
    localparam [143:0] ERROR = "Terminal not ready";
    localparam [143:0] DONE  = "Terminal ready    ";
    localparam [47:0] BOARD = "AX7010";
    localparam [87:0] HEADER = "SYSTEM BOOT";
    wire [143:0] status = timed_out ? ERROR : (percent == 100 ? DONE : START);
    wire in_status = x >= 28 && x < 172 && y >= 231 && y < 247;
    wire in_board = x >= 404 && x < 452 && y >= 231 && y < 247;
    wire in_header = x >= 28 && x < 116 && y >= 29 && y < 45;
    wire [9:0] status_x = x - 28, board_x = x - 404;
    wire [9:0] status_y = y - 231, header_y = y - 29;
    wire [9:0] num_y = y - 115;
    wire [7:0] label = in_board ? BOARD[47 - {board_x[5:3],3'b000} -: 8] :
        (in_header ? HEADER[87 - {status_x[6:3],3'b000} -: 8] :
        status[143 - {status_x[7:3],3'b000} -: 8]);
    wire [3:0] glyph_row = in_number ? num_y/3 : (in_header ? header_y[3:0] : status_y[3:0]);
    wire [2:0] glyph_col = in_number ? number_offset/3 : (in_board ? board_x[2:0] : status_x[2:0]);
    wire [7:0] bits;
    boot_font_rom font (.ch(in_number ? digit : label[6:0]), .row(glyph_row), .data(bits));

    localparam [47:0] LOGO = "AX7010";
    wire [9:0] logo_x = x - 192;
    wire [9:0] logo_y = y - 87;
    wire in_logo = x >= 192 && x < 288 && y >= 87 && y < 103;
    wire [7:0] logo_char = LOGO[47 - {logo_x[8:4], 3'b000} -: 8];
    wire [7:0] logo_font;
    boot_font_rom logo_font_rom (
        .ch(logo_char[6:0]),
        .row(logo_y[3:1]),
        .data(logo_font)
    );
    wire logo_pixel = in_logo && logo_x <= frames && logo_font[7-logo_x[3:1]];

    // Route segments appear in order from the chip towards the outer pads.
    function h;
        input [9:0] px, py, a, b, yy;
        begin h = px >= a && px <= b && py == yy; end
    endfunction
    function v;
        input [9:0] px, py, xx, a, b;
        begin v = py >= a && py <= b && px == xx; end
    endfunction
    function pad;
        input [9:0] px, py, xx, yy;
        begin pad = px >= xx-1 && px <= xx+1 && py >= yy-1 && py <= yy+1; end
    endfunction
    wire route1 = h(x,y,174,191,89) || h(x,y,291,308,89) || h(x,y,174,191,102) || h(x,y,291,308,102);
    wire route2 = v(x,y,174,68,89) || v(x,y,308,68,89) || v(x,y,174,102,122) || v(x,y,308,102,122);
    wire route3 = h(x,y,123,174,68) || h(x,y,308,359,68) || h(x,y,123,174,122) || h(x,y,308,359,122);
    wire route4 = v(x,y,123,55,68) || v(x,y,359,55,68) || v(x,y,123,122,146) || v(x,y,359,122,146);
    wire route5 = h(x,y,81,123,55) || h(x,y,359,401,55) || h(x,y,92,123,146) || h(x,y,359,390,146);
    wire route6 = v(x,y,81,55,76) || v(x,y,401,55,76) || v(x,y,92,146,169) || v(x,y,390,146,169);
    wire route7 = h(x,y,59,81,76) || h(x,y,401,423,76) || h(x,y,66,92,169) || h(x,y,390,416,169);
    wire pads = pad(x,y,59,76) || pad(x,y,423,76) || pad(x,y,66,169) || pad(x,y,416,169);
    wire active_trace = ((percent >=  5) && route1) || ((percent >= 14) && route2) ||
                        ((percent >= 28) && route3) || ((percent >= 42) && route4) ||
                        ((percent >= 56) && route5) || ((percent >= 69) && route6) ||
                        ((percent >= 81) && route7);
    wire corners = ((x == 20 || x == 459) && (y >= 20 && y <= 40 || y >= 231 && y <= 251)) ||
                   ((y == 20 || y == 251) && (x >= 20 && x <= 40 || x >= 439 && x <= 459));
    wire [9:0] bar_width = (percent * 11'd138) / 100;

    always @* begin
        ov_en = de;
        ov_rgb = 8'h25;
        if (corners || h(x,y,28,452,222)) ov_rgb = 8'h26;
        if (route1 || route2 || route3 || route4 || route5 || route6 || route7 || pads) ov_rgb = 8'h27;
        if (active_trace) ov_rgb = 8'h26;
        if (percent >= 90 && pads) ov_rgb = 8'h28;
        if (logo_pixel) ov_rgb = 8'hDB;
        if ((in_number || in_status || in_header || in_board) && bits[7-glyph_col])
            ov_rgb = in_number ? (digit == "%" ? 8'h7F : 8'hDB) : (timed_out && in_status ? 8'hED : 8'h29);
        if (x >= 171 && x < 309 && y >= 175 && y < 177)
            ov_rgb = x < 171 + bar_width ? 8'h7F : 8'h26;
    end
endmodule
