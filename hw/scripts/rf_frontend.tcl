# ---------------------------------------------------------------------------
# rf_frontend.tcl - the VRK160 RF front end as ONE reusable IPI hierarchy.
#
# Sourced by BOTH build flows, so the RF path is defined exactly once:
#   * aie/overlay_rf.tcl       (v++ postSysLink overlay -> XRT + AI Engine design)
#   * hw/scripts/vrk160_ofdm_design.tcl (plain Vivado OFDM-receiver design)
#
#   ADC tile 200 / slice 00 (vin00)  --6.4 GSPS, fine NCO @ RF_NCO_GHZ, DDC /2 /40-->
#   m00_axis 80 MSPS I/Q (1 sample / 80 MHz clk, {Q,I} int16)  --async FIFO-->
#   iq_mux.rf (312.5 MHz datapath clock) ; iq_mux.pb <- DDR playback ; iq_mux.iq_out -> IQ_AXIS
#
# Hierarchy pins
#   PB_AXIS  (slave)  playback samples (mm2s_iq)       IQ_AXIS (master) -> wifi_ddc.iq_in
#   S_AXI_MUX(slave)  iq_mux control  @ PL_IQ_MUX_BASE  S_AXI_RFDC (slave) RF-DC @ PL_RFDC_BASE
#   aclk / aresetn       datapath clock (312.5 MHz)
#   ctrl_clk / ctrl_resetn  RF-DC AXI-Lite clock (100 MHz = RF_AXI_MHZ)
# External ports (top level): adc0_clk (RF tile reference / sampling clock),
#   vin00 (RF input, 2.4 GHz WiFi), sysref_adc_in.
#
# Board-dependent knobs (see docs/RF_FRONTEND.md): the VRK160 RF clock source
# feeding ADC tile 200 must match RF_PLL / RF_REFCLK_MHZ. The defaults use the
# tile PLL from a 200 MHz reference.
# ---------------------------------------------------------------------------
namespace eval ::rf_cfg {
    variable FS_GSPS     6.4      ;# ADC sample rate: 2.4 GHz WiFi in Nyquist zone 1 (5 GHz in zone 2)
    variable PLL         true     ;# tile PLL on (false: adc0_clk is the 6.4 GHz sampling clock)
    variable REFCLK_MHZ  200.000  ;# PLL reference on adc0_clk
    variable NCO_GHZ     2.437    ;# WiFi channel 6; wifi_ddc's PL NCO reaches ch1..ch11
    variable NYQUIST     0        ;# 0 = zone 1 (2.4 GHz); 1 = zone 2 (5 GHz band, NCO 5.18..5.8)
    variable DEC1        1        ;# first-stage DDC: 1 = decimate by 2
    variable DEC2        40       ;# second-stage DDC decimation
    variable WIDTH       2        ;# 16-bit words per fabric clock: 2 = one I/Q sample
    variable AXI_MHZ     100.0    ;# s_axi_aclk frequency
    variable FIFO_DEPTH  512
}

proc rf_frontend_create {name} {
    set h [create_bd_cell -type hier $name]
    set p [current_bd_instance .]
    current_bd_instance $h

    create_bd_intf_pin -mode Slave  -vlnv xilinx.com:interface:axis_rtl:1.0   PB_AXIS
    create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:axis_rtl:1.0   IQ_AXIS
    create_bd_intf_pin -mode Slave  -vlnv xilinx.com:interface:aximm_rtl:1.0  S_AXI_MUX
    create_bd_intf_pin -mode Slave  -vlnv xilinx.com:interface:aximm_rtl:1.0  S_AXI_RFDC
    create_bd_pin -dir I -type clk aclk
    create_bd_pin -dir I -type rst aresetn
    create_bd_pin -dir I -type clk ctrl_clk
    create_bd_pin -dir I -type rst ctrl_resetn

    # ---- RF data converter: ADC tile 200, slice 00 ---------------------------------
    set rf [create_bd_cell -type ip -vlnv xilinx.com:ip:vrf_data_converter:1.2 rfdc_0]
    set_property -dict [list \
        CONFIG.ADC0_Sampling_Rate $::rf_cfg::FS_GSPS \
        CONFIG.ADC0_PLL_Enable $::rf_cfg::PLL \
    ] $rf
    if {$::rf_cfg::PLL} { set_property CONFIG.ADC0_Refclk_Freq $::rf_cfg::REFCLK_MHZ $rf }
    set_property -dict [list \
        CONFIG.ADC_Data_Type00 1 \
        CONFIG.ADC_Mixer_Type00 2 \
        CONFIG.ADC_Mixer_Mode00 0 \
        CONFIG.ADC_NCO_Freq00 $::rf_cfg::NCO_GHZ \
        CONFIG.ADC_Nyquist00 $::rf_cfg::NYQUIST \
        CONFIG.ADC_First_Stage_DDC_Decimation00 $::rf_cfg::DEC1 \
        CONFIG.ADC_Decimation_Mode00 $::rf_cfg::DEC2 \
        CONFIG.ADC_Data_Width00 $::rf_cfg::WIDTH \
        CONFIG.Axiclk_Freq $::rf_cfg::AXI_MHZ \
    ] $rf
    set fab [get_property CONFIG.ADC0_Fabric_Freq $rf]
    set outc [get_property CONFIG.ADC0_Outclk_Freq $rf]
    puts "RF_FRONTEND: ADC fs=$::rf_cfg::FS_GSPS GSPS NCO=$::rf_cfg::NCO_GHZ GHz dec=[expr {2*$::rf_cfg::DEC2}] fabric=$fab MHz outclk=$outc MHz"

    # ---- fabric clock (outclk -> exact fabric rate) + RF-domain reset ----------------
    set cw [create_bd_cell -type ip -vlnv xilinx.com:ip:clkx5_wiz:1.0 rf_clk_wiz]
    set_property -dict [list \
        CONFIG.PRIM_SOURCE {No_buffer} \
        CONFIG.PRIM_IN_FREQ $outc \
        CONFIG.CLKOUT_USED {true,false,false,false,false,false,false} \
        CONFIG.CLKOUT_REQUESTED_OUT_FREQUENCY "$fab,100.000,100.000,100.000,100.000,100.000,100.000" \
        CONFIG.USE_LOCKED {true} \
        CONFIG.USE_RESET {true} \
        CONFIG.RESET_TYPE {ACTIVE_LOW} \
    ] $cw
    set rr [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rf_rst]

    # ---- 80 MHz -> datapath clock CDC ------------------------------------------------
    set ff [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_data_fifo:2.0 rf_fifo]
    set_property -dict [list CONFIG.FIFO_DEPTH $::rf_cfg::FIFO_DEPTH CONFIG.IS_ACLK_ASYNC 1] $ff

    # ---- source select (HLS, hw/ip_repo/iq_mux) --------------------------------------
    set mx [create_bd_cell -type ip -vlnv xilinx.com:hls:iq_mux:1.0 iq_mux_0]

    connect_bd_intf_net [get_bd_intf_pins rfdc_0/m00_axis] [get_bd_intf_pins rf_fifo/S_AXIS]
    connect_bd_intf_net [get_bd_intf_pins rf_fifo/M_AXIS]  [get_bd_intf_pins iq_mux_0/rf]
    connect_bd_intf_net [get_bd_intf_pins PB_AXIS]         [get_bd_intf_pins iq_mux_0/pb]
    connect_bd_intf_net [get_bd_intf_pins iq_mux_0/iq_out] [get_bd_intf_pins IQ_AXIS]
    connect_bd_intf_net [get_bd_intf_pins S_AXI_MUX]       [get_bd_intf_pins iq_mux_0/s_axi_control]
    connect_bd_intf_net [get_bd_intf_pins S_AXI_RFDC]      [get_bd_intf_pins rfdc_0/s_axi]

    connect_bd_net [get_bd_pins rfdc_0/clk_adc0] [get_bd_pins rf_clk_wiz/clk_in1]
    connect_bd_net [get_bd_pins iq_mux_0/rf_resetn] [get_bd_pins rf_clk_wiz/resetn] [get_bd_pins rf_rst/ext_reset_in]
    connect_bd_net [get_bd_pins rf_clk_wiz/locked] [get_bd_pins rf_rst/dcm_locked]
    connect_bd_net [get_bd_pins rf_clk_wiz/clk_out1] [get_bd_pins rfdc_0/m0_axis_aclk] \
        [get_bd_pins rf_fifo/s_axis_aclk] [get_bd_pins rf_rst/slowest_sync_clk]
    connect_bd_net [get_bd_pins rf_rst/peripheral_aresetn] [get_bd_pins rfdc_0/m0_axis_aresetn] \
        [get_bd_pins rf_fifo/s_axis_aresetn]
    connect_bd_net [get_bd_pins aclk] [get_bd_pins rf_fifo/m_axis_aclk] [get_bd_pins iq_mux_0/ap_clk]
    connect_bd_net [get_bd_pins aresetn] [get_bd_pins iq_mux_0/ap_rst_n]
    connect_bd_net [get_bd_pins ctrl_clk] [get_bd_pins rfdc_0/s_axi_aclk]
    connect_bd_net [get_bd_pins ctrl_resetn] [get_bd_pins rfdc_0/s_axi_aresetn]

    current_bd_instance $p
    # RF pins are dedicated package pins of the RF tile: no XDC LOC needed.
    foreach {pin port} {adc0_clk adc0_clk vin00 vin00 sysref_adc_in sysref_adc_in} {
        make_bd_intf_pins_external -name $port [get_bd_intf_pins $name/rfdc_0/$pin]
    }
    return $h
}

# Pin the RF front-end control apertures (same map as hls/common/wifi_common.h).
proc rf_frontend_assign {name {space ""}} {
    set a [list -offset 0xA5000000 -range 64K]
    set b [list -offset 0xA5200000 -range 2M]
    if {$space ne ""} { lappend a -target_address_space $space; lappend b -target_address_space $space }
    assign_bd_address {*}$a [get_bd_addr_segs $name/iq_mux_0/s_axi_control/Reg]
    assign_bd_address {*}$b [get_bd_addr_segs -of [get_bd_intf_pins $name/rfdc_0/s_axi]]
}
