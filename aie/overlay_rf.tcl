## overlay_rf.tcl - v++ POST-SysLink overlay: insert the VRK160 RF front end.
##
## Same technique as the VCK190 design (Wifi-HAR-Versal-VCK190-PL aie/overlay_d2_*):
## v++ owns the AI Engine integration (so the AIE<->PL shim solution and the graph
## CDO are co-generated as one pair); the overlay only INSERTS PL IP on a net v++
## already created and never touches ai_engine_0.
##
##   before:  mm2s_iq/s ------------------------------------------> wifi_ddc/iq_in
##   after :  mm2s_iq/s -> rf/PB_AXIS  [rf: RF-DC + CDC + iq_mux]  rf/IQ_AXIS -> wifi_ddc/iq_in
##
## The post-SysLink BD is FLAT (vrk160_base): control = /ctrl_smc (smartconnect,
## aclk = 156.25 MHz, aclk1 = 312.5 MHz), kernel clock net = clk_wizard_0_clk_out1_o2,
## 100 MHz = clk_wizard_0/clk_out2 (proc_sys_reset_1). Everything is discovered
## from the wifi_ddc pins so a platform rename breaks loudly, not silently.
##
## Env RF_VALIDATE_ONLY=1: stop right after validate_bd_design (fast structural check).
set VALIDATE_ONLY [expr {[info exists ::env(RF_VALIDATE_ONLY)] ? 1 : 0}]
set REPO [file normalize [file join [file dirname [info script]] ..]]
source $REPO/hw/scripts/rf_frontend.tcl
puts "OVERLAY_RF: start on [current_bd_design] (validate_only=$VALIDATE_ONLY)"

# ---- discovery -----------------------------------------------------------------
set ddc_in [get_bd_intf_pins /wifi_ddc/iq_in]
if {$ddc_in eq ""} { error "OVERLAY_RF: /wifi_ddc/iq_in not found (kernel renamed?)" }
set oldnet [get_bd_intf_nets -of $ddc_in]
set srcpin ""
foreach p [get_bd_intf_pins -of $oldnet] {
    if {[get_property PATH $p] ne [get_property PATH $ddc_in]} { set srcpin $p }
}
if {$srcpin eq ""} { error "OVERLAY_RF: no source on the wifi_ddc/iq_in net" }
set aclk_net [get_bd_nets -of [get_bd_pins /wifi_ddc/ap_clk]]
set arst_net [get_bd_nets -of [get_bd_pins /wifi_ddc/ap_rst_n]]
set c100 [get_bd_pins -quiet /clk_wizard_0/clk_out2]
set r100 [get_bd_pins -quiet /proc_sys_reset_1/peripheral_aresetn]
if {$c100 eq "" || $r100 eq ""} { error "OVERLAY_RF: platform 100 MHz clock/reset not found" }
set smc [get_bd_cells -quiet /ctrl_smc]
if {$smc eq ""} {
    set smc [get_bd_cells -of [get_bd_intf_pins -of [get_bd_intf_nets -of [get_bd_intf_pins /wifi_ddc/s_axi_control]] -filter {MODE == Master}]]
}
puts "OVERLAY_RF: pb_src=$srcpin clk=[get_property NAME $aclk_net] ctrl=$smc"

# ---- insert the RF front end ------------------------------------------------------
delete_bd_objs $oldnet
set rf [rf_frontend_create rf]
connect_bd_intf_net $srcpin [get_bd_intf_pins rf/PB_AXIS]
connect_bd_intf_net [get_bd_intf_pins rf/IQ_AXIS] $ddc_in
connect_bd_net -net [get_property NAME $aclk_net] [get_bd_pins rf/aclk]
connect_bd_net -net [get_property NAME $arst_net] [get_bd_pins rf/aresetn]
connect_bd_net $c100 [get_bd_pins rf/ctrl_clk]
connect_bd_net $r100 [get_bd_pins rf/ctrl_resetn]

# ---- control: two more ctrl_smc masters (+ the 100 MHz clock for the RF-DC) --------
set nmi [get_property CONFIG.NUM_MI $smc]
set nck [get_property CONFIG.NUM_CLKS $smc]
set_property -dict [list CONFIG.NUM_MI [expr {$nmi + 2}] CONFIG.NUM_CLKS [expr {$nck + 1}]] $smc
connect_bd_net $c100 [get_bd_pins $smc/aclk$nck]
connect_bd_intf_net [get_bd_intf_pins $smc/[format M%02d_AXI $nmi]]           [get_bd_intf_pins rf/S_AXI_MUX]
connect_bd_intf_net [get_bd_intf_pins $smc/[format M%02d_AXI [expr {$nmi + 1}]]] [get_bd_intf_pins rf/S_AXI_RFDC]

# ---- address map (identical to hls/common/wifi_common.h, host code relies on it) ---
rf_frontend_assign rf
foreach {k off} {wifi_ddc 0xA5010000 ofdm_fe 0xA5020000 ofdm_be 0xA5030000 csi_feat 0xA5040000} {
    assign_bd_address -offset $off -range 64K [get_bd_addr_segs /$k/s_axi_control/Reg]
}

validate_bd_design
puts "OVERLAY_RF: validate_bd_design PASSED"
if {$VALIDATE_ONLY} { error "OVERLAY_RF: validate-only stop (intentional)" }

# ---- top wrapper -------------------------------------------------------------------
# vrk160_base ships a STATIC imported top (imports/hdl/vitis_design_wrapper.v), so the
# RF ports added above would stay unconnected -> tied to GND -> impl error
# "Not legal to attach pin ADC_CLK_P of site RFADC_X0Y0 ... to GND". Regenerate the
# wrapper from the final BD (ports are final here; v++ only assigns addresses later).
save_bd_design
set bdf [get_files -quiet */vitis_design.bd]
set gen [make_wrapper -files $bdf -top -force]
foreach f [get_files -quiet -of_objects [get_filesets sources_1] */vitis_design_wrapper.v] {
    if {[file normalize $f] ne [file normalize $gen]} { remove_files -fileset sources_1 $f }
}
if {[get_files -quiet $gen] eq ""} { add_files -norecurse -fileset sources_1 $gen }
set_property top vitis_design_wrapper [get_filesets sources_1]
set fh [open $gen r]; set wtxt [read $fh]; close $fh
puts "OVERLAY_RF: top wrapper regenerated -> $gen"
foreach l [split $wtxt "\n"] {
    if {[regexp {^\s*(input|output|inout)} $l] && [regexp -nocase {adc|vin|sysref} $l]} { puts "OVERLAY_RF: top port [string trim $l]" }
}
if {[info exists ::env(RF_STOP_AFTER_WRAPPER)]} { error "OVERLAY_RF: stop after wrapper (intentional)" }

# ---- reference capture of the complete integrated BD -------------------------------
# link.sh sets WIFI_HAR_BD_CAPTURE=<repo>/hw/scripts/vrk160_wifi_har_bd.tcl: the BD as
# v++ + this overlay built it (platform, AIE PLIOs, kernels, RF front end), for review
# and for re-creating the block design in a plain Vivado session (see docs).
if {[info exists ::env(WIFI_HAR_BD_CAPTURE)] && $::env(WIFI_HAR_BD_CAPTURE) ne ""} {
    if {[catch {write_bd_tcl -force -no_ip_version $::env(WIFI_HAR_BD_CAPTURE)} e]} {
        puts "OVERLAY_RF: WARNING: BD capture failed: $e"
    } else {
        puts "OVERLAY_RF: BD captured -> $::env(WIFI_HAR_BD_CAPTURE)"
    }
}
