## overlay_rf_pre.tcl - v++ PRE-SysLink overlay: make the repo's Vivado IP
## (hw/ip_repo: iq_mux) visible to v++'s Vivado project before the BD is built,
## so overlay_rf.tcl can instantiate it. Appends (never replaces) the repo list.
set _repo [file normalize [file join [file dirname [info script]] .. hw ip_repo]]
set _cur  [get_property ip_repo_paths [current_project]]
if {[lsearch -exact $_cur $_repo] < 0} {
    set_property ip_repo_paths [concat $_cur [list $_repo]] [current_project]
}
update_ip_catalog -rebuild
if {[llength [get_ipdefs -quiet xilinx.com:hls:iq_mux:*]] == 0} {
    error "PRE_RF: iq_mux IP not found in $_repo (run hls/build_kernels.sh first)"
}
puts "PRE_RF: ip_repo $_repo added (iq_mux found)"
