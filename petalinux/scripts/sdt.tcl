# sdt.tcl - generate a System Device Tree (SDT) from an XSA for the VRK160.
# Usage: sdtgen sdt.tcl <design.xsa> <sdt_outdir>      (or: xsct sdt.tcl ...)
# The VRK160 (xcvr1602, ES1) has no board DTS in the 2025.2 device-tree repo;
# board specifics (none needed for this design) would go in system-user.dtsi.
set xsa    [lindex $argv 0]
set outdir [lindex $argv 1]
file delete -force $outdir
sdtgen set_dt_param -xsa $xsa -dir $outdir
sdtgen generate_sdt
