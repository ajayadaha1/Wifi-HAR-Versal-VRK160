# RF front end — Versal RF data converter on the VRK160

The RF front end is ONE IP-Integrator hierarchy, [`hw/scripts/rf_frontend.tcl`](../hw/scripts/rf_frontend.tcl),
used unchanged by both builds (the v++ overlay [`aie/overlay_rf.tcl`](../aie/overlay_rf.tcl)
and the plain design [`hw/scripts/vrk160_ofdm_design.tcl`](../hw/scripts/vrk160_ofdm_design.tcl)).

```
 vin00 (2.4 GHz WiFi) ─► rfdc_0  ADC tile 200 / slice 00, 6.4 GSPS, real→I/Q
                         fine NCO @ RF_NCO_GHZ, DDC ÷2 (1st stage) ÷40 (2nd stage)
 adc0_clk ─────────────► tile PLL (200 MHz ref) ── clk_adc0 (100 MHz) ─► rf_clk_wiz ─► 80 MHz fabric clk
 sysref_adc_in ────────► (unused: single tile, no multi-tile sync)
                         m00_axis 80 MSPS, 32 bit {Q,I} ─► rf_fifo (async, 512) ─► iq_mux.rf  @312.5 MHz
 mm2s_iq (DDR playback) ─────────────────────────────────────────────────────────► iq_mux.pb
                                                                     iq_mux.iq_out ─► wifi_ddc
```

## Configuration (all in `namespace ::rf_cfg`)

| Knob | Default | Meaning |
|---|---|---|
| `FS_GSPS` | 6.4 | ADC sample rate. 2.4 GHz WiFi sits in Nyquist zone 1 (0–3.2 GHz); the 5 GHz band would be zone 2 |
| `PLL` / `REFCLK_MHZ` | true / 200.000 | tile PLL from a 200 MHz reference on `adc0_clk` (**board-dependent, see below**) |
| `NCO_GHZ` | 2.437 | RF-DC fine NCO = WiFi channel 6 |
| `NYQUIST` | 0 | 0 = zone 1 (2.4 GHz). For 5 GHz: `NCO_GHZ 5.18..5.825`, `NYQUIST 1` |
| `DEC1` / `DEC2` | 1 (÷2) / 40 | 6.4 GSPS ÷ 80 = **80 MSPS** complex |
| `WIDTH` | 2 | two 16-bit words per fabric clock = one I/Q sample → **80 MHz** fabric clock (`clk_adc0` = 100 MHz outclk, MMCM 100→80) |
| `AXI_MHZ` | 100 | `s_axi_aclk` = platform 100 MHz clock |

The Vivado IP computes and reports the derived fabric/outclk rates; the overlay
prints `RF_FRONTEND: ADC fs=6.4 GSPS NCO=2.437 GHz dec=80 fabric=80.000 MHz outclk=100.000 MHz`.

### Why these numbers
* 80 MSPS keeps a ±40 MHz window around the RF-DC NCO: the PL NCO in `wifi_ddc`
  selects **any of channels 1…11 at run time** (`host_wifi live --chan N`, offset
  −25…+25 MHz) without re-configuring the converter.
* 80 → 20 MSPS is done in the PL (`wifi_ddc`, 64-tap FIR) where the pass band can
  be made flat to 8.6 MHz — the 802.11 edge subcarriers are at ±8.125 MHz.
* 6.4 GSPS supports both WiFi bands with one tile type (quad tile, ≤ 8 GSPS).

## Board-dependent items (verify on your VRK160 before live RF)

The 2025.2 VRK160 board files (`xilinx.com:vrk160:part0:1.1`) do not describe the
RF clocking, so these are **assumptions** made explicit in `rf_frontend.tcl`:

1. **ADC tile 200 reference clock**: 200 MHz on the tile-200 clock input, tile PLL
   enabled. If your board's RF clock tree delivers a different reference, set
   `rf_cfg::REFCLK_MHZ` (valid PLL references at 6.4 GSPS: 103.2 … 800 MHz, e.g.
   160, 200, 256, 320, 400, 800) or `rf_cfg::PLL false` with a 6.4 GHz sampling
   clock, and re-run the build. The RF clock synthesisers are programmed by the
   board's system controller / clock utility, not by this design.
2. **RF input**: WiFi into `vin00` of ADC tile 200 through the board's balun /
   SMA path. An antenna directly on the ADC input works only for a nearby
   transmitter; for room-scale sensing add a 2.4 GHz band-pass filter + LNA
   (20–30 dB). Keep the input below full scale (the receiver works down to
   about −60 dBFS; `host_wifi live --gain 0..7` adds PL digital gain).
3. **I/Q packing / spectral orientation** of `m00_axis` is taken as `{Q[31:16], I[15:0]}`
   in zone 1. If live packets are never detected while `rf_count` climbs (ADC
   streaming) but a strong transmitter is present, try `wifi_ddc ctrl` bit 0
   (swap I/Q) or bit 1 (conjugate) — both are run-time register bits.

## Bring-up checks (from Linux)

After the PL + AIE image is loaded (the `wifi-har` service runs `fpgautil -b
wifi_har_vrk160.pdi -o wifi_har_vrk160.dtbo`; the RF-DC IP powers its tiles up with
the configured settings — no run-time converter driver is used by this design, so
`rf_count` is the check that the tile actually started):
```sh
./host_wifi status                       # STAT,<rf_count>,...  rf_count must climb in live mode
./host_wifi live wifi_har_vrk160.xclbin --chan 6 | head      # CSI,/STAT, lines
```
`rf_count` (iq_mux register 0x18) counts RF samples: ~80 M per second when the
ADC tile is up and the fabric MMCM has locked after `iq_mux.sel[2]` released the
RF-domain reset. `p_now` (ofdm_fe 0x48) is the input power over 48 samples.

## What a WiFi source needs to provide

Any 802.11a/g/n/ac/ax transmitter on the selected 20 MHz channel: every OFDM PPDU
starts with the legacy L-STF/L-LTF/L-SIG preamble the receiver uses (DSSS 802.11b
frames are not OFDM and are ignored). CSI is sampled per packet, so the packet
rate sets the sensing rate: an AP's beacons give ~10/s; a phone or Raspberry Pi
running `ping -i 0.02 <AP>` gives ≥ 50/s. Live mode bins CSI at 20 Hz
(`--bin-us 50000`), matching the AIE motion band-pass design (1–3 Hz at 20 Hz).
