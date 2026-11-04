set_property PACKAGE_PIN U18 [get_ports sys_clk]
set_property IOSTANDARD LVCMOS33 [get_ports sys_clk]
create_clock -period 20 [get_ports sys_clk]

############## 4.3 inch 480x272 LCD ##################
set_property PACKAGE_PIN W13 [get_ports lcd_dclk]
set_property PACKAGE_PIN V12 [get_ports lcd_hs]
set_property PACKAGE_PIN U12 [get_ports lcd_vs]
set_property PACKAGE_PIN T12 [get_ports lcd_de]

set_property PACKAGE_PIN W19 [get_ports {lcd_r[0]}]
set_property PACKAGE_PIN W18 [get_ports {lcd_r[1]}]
set_property PACKAGE_PIN R14 [get_ports {lcd_r[2]}]
set_property PACKAGE_PIN P14 [get_ports {lcd_r[3]}]
set_property PACKAGE_PIN Y17 [get_ports {lcd_r[4]}]
set_property PACKAGE_PIN Y16 [get_ports {lcd_r[5]}]
set_property PACKAGE_PIN W15 [get_ports {lcd_r[6]}]
set_property PACKAGE_PIN V15 [get_ports {lcd_r[7]}]

set_property PACKAGE_PIN Y14 [get_ports {lcd_g[0]}]
set_property PACKAGE_PIN W14 [get_ports {lcd_g[1]}]
set_property PACKAGE_PIN P18 [get_ports {lcd_g[2]}]
set_property PACKAGE_PIN N17 [get_ports {lcd_g[3]}]
set_property PACKAGE_PIN U15 [get_ports {lcd_g[4]}]
set_property PACKAGE_PIN U14 [get_ports {lcd_g[5]}]
set_property PACKAGE_PIN P16 [get_ports {lcd_g[6]}]
set_property PACKAGE_PIN P15 [get_ports {lcd_g[7]}]

set_property PACKAGE_PIN U17 [get_ports {lcd_b[0]}]
set_property PACKAGE_PIN T16 [get_ports {lcd_b[1]}]
set_property PACKAGE_PIN V18 [get_ports {lcd_b[2]}]
set_property PACKAGE_PIN V17 [get_ports {lcd_b[3]}]
set_property PACKAGE_PIN T15 [get_ports {lcd_b[4]}]
set_property PACKAGE_PIN T14 [get_ports {lcd_b[5]}]
set_property PACKAGE_PIN V13 [get_ports {lcd_b[6]}]
set_property PACKAGE_PIN U13 [get_ports {lcd_b[7]}]

set_property IOSTANDARD LVCMOS33 [get_ports lcd_dclk]
set_property IOSTANDARD LVCMOS33 [get_ports lcd_hs]
set_property IOSTANDARD LVCMOS33 [get_ports lcd_vs]
set_property IOSTANDARD LVCMOS33 [get_ports lcd_de]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_r[*]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_g[*]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_b[*]}]


# PS AXI GPIO to the first stage of the LCD pixel-clock synchronizer.
set_false_path -to [get_pins {boot_done_sync_reg[0]/D}]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]

# Budget setup and hold at either DCLK edge for the external panel.
create_generated_clock -name panel_dclk -source [get_pins lcd_clock_out/C] -divide_by 1 [get_ports lcd_dclk]
set panel_data [get_ports {lcd_r[*] lcd_g[*] lcd_b[*] lcd_hs lcd_vs lcd_de}]
set_output_delay -clock panel_dclk -max 5 $panel_data
set_output_delay -clock panel_dclk -min -5 $panel_data
set_output_delay -clock panel_dclk -clock_fall -add_delay -max 5 $panel_data
set_output_delay -clock panel_dclk -clock_fall -add_delay -min -5 $panel_data
