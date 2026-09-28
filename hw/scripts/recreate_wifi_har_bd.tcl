# ---------------------------------------------------------------------------
# recreate_wifi_har_bd.tcl - re-create the complete integrated WiFi-HAR block design
# (vrk160_base platform + PL kernels + AI Engine PLIOs + RF front end, exactly as
# v++ and aie/overlay_rf.tcl built it) in a plain Vivado project, for review / debug:
#
#   vivado -mode batch -source hw/scripts/recreate_wifi_har_bd.tcl [-tclargs <project_dir>]
#
# Input: vrk160_wifi_har_bd.tcl, the raw write_bd_tcl capture that the overlay writes at
# every link. PREREQ: hls/build_kernels.sh (HLS IPs in hw/ip_repo).
# The deployable design (with the compiled AIE graph, the PDI and the xclbin) is built
# by aie/link.sh + aie/package.sh; this BD carries ai_engine_0 with its PLIO ports but
# no graph, so it is meant for inspection, not for a bitstream.
# ---------------------------------------------------------------------------
set HERE [file normalize [file dirname [info script]]]
set PRJ  [expr {[llength $argv] ? [file normalize [lindex $argv 0]] : [file normalize $HERE/../build/wifi_har_bd]}]

catch {enable_beta_device xcvr16*}
create_project -force wifi_har_bd $PRJ -part xcvr1602-vsva2488-2MP-e-S-es1
set_property board_part xilinx.com:vrk160:part0:1.1 [current_project]
set_property ip_repo_paths [list [file normalize $HERE/../ip_repo]] [current_project]
update_ip_catalog

# The capture ends with validate_bd_design, which fails until the two fixes below are
# applied; the block design itself is complete at that point.
if {[catch {source $HERE/vrk160_wifi_har_bd.tcl} e]} {
    puts "RECREATE: capture sourced, finishing the design ($e)"
}

# 1) the platform NoC / PS instances were captured with TLM simulation models next to
#    RTL ones (allowed inside v++, an error in a plain project) -> all RTL
foreach c [get_bd_cells -hier -filter {SELECTED_SIM_MODEL == tlm}] {
    set_property SELECTED_SIM_MODEL rtl $c
}
# 2) v++ assigns the XRT movers' apertures after the overlay captured the BD
#    (same offsets as in the linked design; XRT reads them from the xclbin)
foreach {cell off} {mm2s_iq 0xA4010000 s2mm_csi 0xA4020000 s2mm_feat 0xA4030000} {
    assign_bd_address -offset $off -range 64K [get_bd_addr_segs /$cell/s_axi_control/Reg]
}
assign_bd_address

validate_bd_design
save_bd_design
make_wrapper -files [get_files vitis_design.bd] -top -import -force
update_compile_order -fileset sources_1
puts "RECREATE_WIFI_HAR_BD_OK $PRJ ([llength [get_bd_cells -hier]] cells)"
