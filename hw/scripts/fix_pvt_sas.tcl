# ---------------------------------------------------------------------------
# fix_pvt_sas.tcl - implementation hook (impl_1 STEPS.OPT_DESIGN.TCL.PRE) for both
# flows (aie/link.sh via --vivado.prop, hw/scripts/project_top.tcl directly).
#
# vrf_data_converter 1.2 (Vivado 2025.2) instantiates two PVT_SAS temperature
# readouts in its register block (MicroBlaze start-up temperature, "DAC tile 0" /
# "ADC tile 3") but the xcvr1602 ES1 device model has NO PVT_SAS site, so placement
# stops with  [DRC UTLZ-1] PVT_SAS over-utilized ... requires 2 ... no compatible
# site.  (2026.1 / v1.3 replaced them with per-converter SYSMON satellites.)
# Remove the two cells and drive their TVALUE loads with constant 0 - the value of
# the IP's own commented-out fallback (`//assign dac0_mb_temp = 16'd0;`). The
# start-up state machine then reads 0 degC instead of the die temperature.
# ---------------------------------------------------------------------------
set _cells [get_cells -quiet -hierarchical -filter {REF_NAME == PVT_SAS}]
puts "FIX_PVT_SAS: [llength $_cells] PVT_SAS cell(s) found"
set _k 0
foreach _c $_cells {
    set _parent [get_property PARENT $_c]
    foreach _p [get_pins -quiet -of_objects $_c -filter {DIRECTION == OUT}] {
        set _n [get_nets -quiet -of_objects $_p]
        if {$_n eq ""} { continue }
        set _g [create_cell -reference GND [expr {$_parent eq "" ? "" : "$_parent/"}]pvt_sas_tie0_$_k]
        incr _k
        disconnect_net -net $_n -objects $_p
        connect_net -net $_n -objects [get_pins $_g/G]
    }
    puts "FIX_PVT_SAS: removing $_c"
    remove_cell $_c
}
puts "FIX_PVT_SAS: done ($_k output net(s) tied to 0)"
