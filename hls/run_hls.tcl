# ---------------------------------------------------------------------------
# run_hls.tcl - build ONE PL kernel with Vitis HLS 2025.2 for the VRK160.
#   K=<top> SRC=<file under hls/> TBDEF=<K_...> [EXTRA_TB="<files>"] [FLOW=...]
#   vitis-run --mode hls --tcl run_hls.tcl        (driven by build_kernels.sh)
# FLOW (comma list, default csim,csynth,export): csim csynth cosim export
#   export -> <OUT>/<K>.xo (v++ link, XRT design) AND <IPREPO>/<K>/ (Vivado IP,
#   plain design), both from the SAME synthesised RTL.
# The xcvr1602 is an ES1 device: enable_beta_device must run before set_part.
# ---------------------------------------------------------------------------
enable_beta_device *
set HLS   [file normalize [file dirname [info script]]]
set K     $::env(K)
set SRC   $::env(SRC)
set TBDEF $::env(TBDEF)
set FLOW  [expr {[info exists ::env(FLOW)] ? $::env(FLOW) : "csim,csynth,export"}]
set PART  [expr {[info exists ::env(PART)] ? $::env(PART) : "xcvr1602-vsva2488-2MP-e-S-es1"}]
set CLK   [expr {[info exists ::env(CLK)] ? $::env(CLK) : "3.2"}]
set OUT   [expr {[info exists ::env(OUT)] ? $::env(OUT) : "$HLS/build/xo"}]
set IPREPO [expr {[info exists ::env(IPREPO)] ? $::env(IPREPO) : "[file normalize $HLS/../hw/ip_repo]"}]
set CF    "-I$HLS/common"
# co-sim of the non-II=1 free-running kernels is only supported with ap_ctrl_hs:
# HLS_COSIM_HS swaps the control protocol (datapath RTL unchanged); never exported.
if {[string first cosim [expr {[info exists ::env(FLOW)] ? $::env(FLOW) : ""}]] >= 0} {
    append CF " -DHLS_COSIM_HS"
}
# vitis = XRT kernel (.xo + IP); vivado = IP only (iq_mux: needs an ap_none wire port)
set TGT   [expr {[info exists ::env(FLOWTGT)] ? $::env(FLOWTGT) : "vitis"}]

open_project -reset ${K}_prj
set_top $K
add_files $HLS/$SRC -cflags $CF
add_files -tb $HLS/tb/tb_kernel.cpp -cflags "$CF -D$TBDEF"
if {[info exists ::env(EXTRA_TB)]} {
    foreach f $::env(EXTRA_TB) { add_files -tb $HLS/$f -cflags $CF }
}
open_solution -reset sol1 -flow_target $TGT
set_part $PART
create_clock -period $CLK -name default
config_interface -m_axi_addr64
# Bit-exactness with the C model (golden_chain): no inferred DSPFP32 accumulators.
# The 'standard' facc/fmacc cores re-round the running sum differently from a
# sequential C loop (csi_feat window sums were a few ULP off in C/RTL co-sim);
# plain fadd/fmul chains are IEEE-identical and still reach II=1 at 312.5 MHz.
config_op facc -impl none
config_op fmacc -impl none

set flows [split $FLOW ,]
if {[lsearch $flows csim]   >= 0} { csim_design -O }
if {[lsearch $flows csynth] >= 0 || [lsearch $flows cosim] >= 0 || [lsearch $flows export] >= 0} { csynth_design }
if {[lsearch $flows cosim]  >= 0} { cosim_design -O -trace_level none }
if {[lsearch $flows export] >= 0 && [lsearch $flows cosim] < 0} {
    file mkdir $OUT
    if {$TGT eq "vitis"} { export_design -format xo -output $OUT/$K.xo }
    export_design -format ip_catalog -output $OUT/${K}_ip.zip
    file delete -force $IPREPO/$K
    file mkdir $IPREPO/$K
    exec unzip -q -o $OUT/${K}_ip.zip -d $IPREPO/$K
    puts "EXPORTED $K ($TGT) -> $OUT and $IPREPO/$K"
}
exit
