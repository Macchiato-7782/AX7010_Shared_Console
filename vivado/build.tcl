set root [file dirname [file normalize [info script]]]
cd $root
create_project -force a2_dual project -part xc7z010clg400-1
set_property ip_repo_paths [list [file join $root ip_repo]] [current_project]
update_ip_catalog
source vendor_03_bd.tcl
set bd [get_files design_1.bd]
open_bd_design $bd

set interconnect [get_bd_cells ps7_0_axi_periph]
set_property CONFIG.NUM_MI 7 $interconnect
set bram_ctrl [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_bram_ctrl:4.0 axi_bram_ctrl_fb]
set_property -dict [list CONFIG.SINGLE_PORT_BRAM 1 CONFIG.DATA_WIDTH 32] $bram_ctrl
set bram [create_bd_cell -type ip -vlnv xilinx.com:ip:blk_mem_gen:8.4 blk_mem_gen_fb]
set_property -dict [list CONFIG.Interface_Type Native CONFIG.Memory_Type True_Dual_Port_RAM CONFIG.Write_Width_A 32 CONFIG.Read_Width_A 32 CONFIG.Write_Depth_A 32640 CONFIG.Operating_Mode_A WRITE_FIRST CONFIG.Write_Width_B 32 CONFIG.Read_Width_B 32 CONFIG.Operating_Mode_B READ_FIRST] $bram
set ready [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 axi_gpio_boot]
set_property -dict [list CONFIG.C_GPIO_WIDTH 1 CONFIG.C_ALL_OUTPUTS 1 CONFIG.C_DOUT_DEFAULT 0x00000000] $ready

connect_bd_intf_net [get_bd_intf_pins $interconnect/M05_AXI] [get_bd_intf_pins $bram_ctrl/S_AXI]
connect_bd_intf_net [get_bd_intf_pins $interconnect/M06_AXI] [get_bd_intf_pins $ready/S_AXI]
connect_bd_intf_net [get_bd_intf_pins $bram_ctrl/BRAM_PORTA] [get_bd_intf_pins $bram/BRAM_PORTA]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
    [get_bd_pins $interconnect/M05_ACLK] [get_bd_pins $interconnect/M06_ACLK] \
    [get_bd_pins $bram_ctrl/s_axi_aclk] [get_bd_pins $ready/s_axi_aclk]
connect_bd_net [get_bd_pins rst_processing_system7_0_100M/peripheral_aresetn] \
    [get_bd_pins $interconnect/M05_ARESETN] [get_bd_pins $interconnect/M06_ARESETN] \
    [get_bd_pins $bram_ctrl/s_axi_aresetn] [get_bd_pins $ready/s_axi_aresetn]
set lcd_bram [create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:bram_rtl:1.0 lcd_bram]
set_property CONFIG.MASTER_TYPE BRAM_CTRL $lcd_bram
connect_bd_intf_net $lcd_bram [get_bd_intf_pins $bram/BRAM_PORTB]
connect_bd_net [create_bd_port -dir O -from 0 -to 0 boot_done] [get_bd_pins $ready/gpio_io_o]

set space [get_bd_addr_spaces processing_system7_0/Data]
create_bd_addr_seg -range 0x20000 -offset 0x40000000 $space \
    [get_bd_addr_segs $bram_ctrl/S_AXI/Mem0] SEG_axi_bram_ctrl_fb_Mem0
create_bd_addr_seg -range 0x10000 -offset 0x41220000 $space \
    [get_bd_addr_segs $ready/S_AXI/Reg] SEG_axi_gpio_boot_Reg
validate_bd_design
save_bd_design
generate_target all $bd
add_files -norecurse [make_wrapper -files $bd -top]
add_files -norecurse {a2_fb43_top.v video_clock.v lcd_fb_scanout.v boot_overlay.v boot_font_rom.v}
add_files -fileset constrs_1 -norecurse {hdmi_out.xdc btns_leds.xdc lcd43.xdc}
set_property top a2_fb43_top [current_fileset]
update_compile_order -fileset sources_1
launch_runs synth_1 -jobs 6
wait_on_run synth_1
if {![string match "*Complete*" [get_property STATUS [get_runs synth_1]]]} {error "Synthesis failed"}
launch_runs impl_1 -to_step write_bitstream -jobs 6
wait_on_run impl_1
if {![string match "*Complete*" [get_property STATUS [get_runs impl_1]]]} {error "Implementation failed"}
file copy -force project/a2_dual.runs/impl_1/a2_fb43_top.bit dual_console.bit
file copy -force project/a2_dual.runs/impl_1/a2_fb43_top_timing_summary_routed.rpt timing.rpt
puts "A2_BUILD_OK"

