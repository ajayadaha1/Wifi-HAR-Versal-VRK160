# WiFi-CSI Human Activity Recognition on the Versal RF VRK160 (RF-ADC + PL OFDM receiver + AI Engine)

The **VRK160 is the WiFi sensor**: its RF-ADC samples the 2.4 GHz band directly, an
**802.11 OFDM receiver in the Programmable Logic** detects every packet and extracts
its **Channel State Information (CSI)**, a PL conditioner turns the CSI stream into
time series that the **AI Engine** reduces to motion / breathing / phase features,
and only compact records are DMA'd to Linux on the Arm Cortex-A72, where a
board-hosted **dashboard** shows activity, presence, breathing rate and falls live.

This is the VRK160 port of [Wifi-HAR-Versal-VCK190-PL](../Wifi-HAR-Versal-VCK190-PL)
(there the CSI came from a Raspberry Pi running nexmon_csi over Ethernet) and follows
its technique and conventions: HLS kernels, the same pure-C AIE feature graph, **v++
co-generation with a postSysLink overlay**, XRT-managed DDR buffers, a tracked
PetaLinux project, a stdlib-only Python dashboard, and a bit-exact golden model.

> Design docs: [`docs/plan.md`](docs/plan.md) (architecture & decisions) ·
> [`docs/OFDM_RX.md`](docs/OFDM_RX.md) (receiver, records, register map) ·
> [`docs/RF_FRONTEND.md`](docs/RF_FRONTEND.md) (RF-DC config, board assumptions) ·
> [`docs/PROJECT_STATE.md`](docs/PROJECT_STATE.md) (build log, results, gotchas).

```
 2.4 GHz ─► RF-ADC 6.4 GSPS ─ DDC ─► 80 MSPS ─┐
                                               ├─ iq_mux ─ wifi_ddc ─ ofdm_fe ─ ofdm_be ─┬─ csi_feat ─ AI Engine ─ feat_pack ─ s2mm_feat ─► DDR
 DDR ─► mm2s_iq (playback / self-test) ────────┘   (NCO+/4)  (STF/LTF)  (CFO,FFT,  └─ s2mm_csi (CSI records) ──────────────────────► DDR
                                                                        CSI,L-SIG)
 A72 Linux: host_wifi (XRT) ─► FEAT / CSI / STAT lines ─► live_dashboard.py  http://<board>:8080
```

---

## Status

| Stage | Result |
|---|---|
| OFDM receiver algorithm (golden model vs Python PHY model, 2 920 packets, 30 dB) | **100 % detected, 100 % L-SIG RATE/LENGTH, CSI error 2.1 %, CFO ±0.5 kHz** |
| HLS kernels (8) — C-sim | bit-exact vs the golden streams |
| HLS kernels — C/RTL co-simulation | **bit-exact** (wifi_ddc, ofdm_fe, ofdm_be, csi_feat, feat_pack, movers) |
| HLS timing @ 312.5 MHz (3.2 ns) | all kernels ≤ 2.98 ns estimated |
| Deterministic restart after any prior traffic | 145 prefix cut points → byte-identical CSI / features (golden model) |
| AIE feature graph (AIE1, vrk160_base) | 4 kernels on 2 tiles, x86sim bit-exact vs the golden cores — same kernels as the VCK190 silicon-validated graph |
| Features → activity (golden AIE outputs) | walking/empty motion ratio 36 000×, breathing 15.0 bpm (15 injected), fall detected once, no false fall |
| v++ link + RF overlay → XSA / xclbin / run-time PDI+dtbo | **timing met, WNS +0.214 ns** @ 312.5 MHz; 8.9 % LUT, 6.8 % DSP ([PROJECT_STATE §6.3](docs/PROJECT_STATE.md)) |
| Plain-Vivado OFDM-receiver design (Tcl) → XSA | **timing met, WNS +0.221 ns**; 6.2 % LUT |
| PetaLinux 2025.2 project (`petalinux/`, SDT flow, XRT + zocl + AIE driver + FPGA manager + python3) | SD image: `BOOT.BIN`, `boot.scr`, `image.ub` ([PROJECT_STATE §7](docs/PROJECT_STATE.md)) |
| Dashboard (replay of the golden pipeline output, any PC) | runs; activity / breathing / fall / CSI waterfall verified |
| On-silicon run | not yet (needs a VRK160 slot; live RF additionally needs an antenna on ADC tile 200) |

---

## Repository layout

| Path | Contents |
|---|---|
| `csi_ref/` | **golden PHY model**: 802.11a/g TX (L-STF/LTF/SIG/DATA), human-activity multipath channel, float reference RX (`wifi_ofdm.py`); vector generator (`make_vectors.py`), cross-checker (`check_golden.py`), ruview 8-feature packing (`features8.py`) |
| `hls/` | PL kernels: `wifi_ddc/` (iq_mux, wifi_ddc), `ofdm_rx/` (ofdm_fe, ofdm_be), `csi_feat/` (csi_feat, feat_pack), `movers/` (mm2s, s2mm); `common/` (formats, generated tables, deterministic float math); `tb/` (bit-exact **golden_chain**, per-kernel C/RTL testbench); `run_hls.tcl` + `build_kernels.sh` |
| `aie/src/` | AIE feature graph (FIR→stats, windowed DFT, stats) — the VCK190 pure-C kernels, re-targeted |
| `aie/` | `build_hw.sh` (aiecompiler), `run_x86sim.sh` + `compare.py` (x86sim vs golden), `system.cfg` (v++ connectivity), `overlay_rf_pre.tcl` + `overlay_rf.tcl` (v++ pre/post-SysLink overlays: RF front end, address map, top wrapper, BD capture), `link.sh`, `package.sh` |
| `aie/host/` | XRT host `host_wifi.cpp` (selftest / play / live / status) + `pl_map.h` |
| `hw/scripts/` | **Vivado Tcl**: `project_top.tcl` + `vrk160_ofdm_design.tcl` (plain OFDM-receiver design → XSA), `rf_frontend.tcl` (RF hierarchy shared with the overlay), `fix_pvt_sas.tcl` (impl hook), `vrk160_wifi_har_bd.tcl` (capture of the full v++ BD incl. AIE, written at every link) + `recreate_wifi_har_bd.tcl` (re-creates and validates that BD in a plain Vivado project) |
| `hw/ip_repo/` | HLS kernels packaged as Vivado IP (for the plain design and the overlay's `iq_mux`) |
| `live/` | `live_dashboard.py` — board-hosted SSE dashboard (sources: exec / replay / fifo) |
| `sw/` | `ofdm_ctl.py` — `/dev/mem` control for the plain design (no XRT) |
| `petalinux/` | **PetaLinux 2025.2 project** (`project-spec/`: configs, `wifi-har` app recipe + boot service, device tree; `scripts/`: `sdt.tcl`, `import_hw.sh`, `build_boot.sh`, `plain_design.dtsi`) |
| `docs/` | plan, receiver/RF design references, project state; `reports/` (post-route timing + utilisation of both designs) |
| `build_vivado.sh`, `build_petalinux.sh` | one-shot builds |

---

## Requirements

* **VRK160** (xcvr1602-vsva2488-2MP-e-S-**es1**) — ES1 part: add `enable_beta_device *`
  to `~/.Xilinx/Vivado/Vivado_init.tcl` (the HLS scripts enable it themselves).
* **Vivado / Vitis 2025.2** (platform `Vitis/base_platforms/vrk160_base`), **PetaLinux 2025.2**.
* Python 3 with numpy + scipy for the golden model (`pip install numpy scipy`).
* For live RF: 2.4 GHz antenna (+ band-pass/LNA) on ADC tile 200 `vin00`, the board's
  RF reference clock as in [`docs/RF_FRONTEND.md`](docs/RF_FRONTEND.md), and WiFi traffic
  on the chosen channel (e.g. `ping -i 0.02` to the AP from a phone / Raspberry Pi).

## Build

```bash
# 0) golden model sanity check (no tools needed except g++ + HLS headers)
make -C hls/tb check PY=python3               # 40 packets: detection, L-SIG, CSI, CFO -> CHECK_GOLDEN PASS

# 1) hardware: golden vectors, HLS kernels, AIE graph, v++ link (+RF overlay), package
./build_vivado.sh                             # -> aie/wifi_har_vrk160.{xsa,xclbin}, aie/package/wifi_har_vrk160.{pdi,dtbo}
PLAIN=1 ./build_vivado.sh                     # additionally: plain OFDM-RX design hw/build/vrk160_ofdm_rx.xsa

# 2) Linux image (PetaLinux project in ./petalinux; TMPDIR is forced to /tmp/petalinux-$USER)
./build_petalinux.sh                          # -> petalinux/images/linux/sd/{BOOT.BIN,boot.scr,image.ub}
```

Individual steps: `hls/build_kernels.sh [kernel..]` (`FLOW=csim,csynth,cosim` for co-sim),
`aie/build_hw.sh`, `RF_VALIDATE_ONLY=1 aie/link.sh` (3-min structural check of the
overlay), `aie/link.sh`, `aie/package.sh`,
`vivado -mode batch -source hw/scripts/project_top.tcl [-tclargs bd_only]`,
`vivado -mode batch -source hw/scripts/recreate_wifi_har_bd.tcl` (the integrated
WiFi-HAR block design in a plain Vivado project, for inspection).

## Run

Copy `petalinux/images/linux/sd/*` to the FAT partition of an SD card and boot
(SD boot mode). vrk160_base uses **Versal segmented configuration**: `BOOT.BIN` only
brings up the PS, NoC and DDR; the `wifi-har` service then

0. loads the **PL + AI Engine image** — `fpgautil -b wifi_har_vrk160.pdi -o
   wifi_har_vrk160.dtbo` (PLD PDI + AIE graph CDO; the overlay adds the PL, AIE and
   zocl nodes; console: `wifi-har: PL+AIE image load: operating`);
1. runs the **self-test**: the 40-packet golden IQ file is played from DDR through the
   whole chain (wifi_ddc → OFDM RX → csi_feat → AIE) and every CSI record / feature is
   compared with the bit-exact model — the console shows `##### WIFI-HAR SELFTEST ... PASS`;
2. starts the **dashboard** on port 8080 — `MODE=demo` (default) plays the 146 s activity
   scenario from DDR in real time; `MODE=live` uses the RF-ADC (`/home/root/wifi-har/wifi-har.conf`).

Manually on the board (`/home/root/wifi-har`):
```sh
fpgautil -b wifi_har_vrk160.pdi -o wifi_har_vrk160.dtbo       # once per boot (the service does it)
./host_wifi selftest wifi_har_vrk160.xclbin vectors/quick     # golden check -> SELFTEST PASS
./host_wifi play wifi_har_vrk160.xclbin vectors/demo --loop   # FEAT/CSI/STAT lines
./host_wifi live wifi_har_vrk160.xclbin --chan 6               # live RF, 2.4 GHz channel 1..11
python3 live_dashboard.py --source exec --cmd "./host_wifi live wifi_har_vrk160.xclbin --chan 6"
```
**Plain OFDM-receiver design** (no AIE/XRT, `hw/build/vrk160_ofdm_rx.xsa`): disable
*FPGA Manager* in `petalinux-config`, `petalinux/scripts/import_hw.sh hw/build/vrk160_ofdm_rx.xsa`,
append `petalinux/scripts/plain_design.dtsi` (DMA carve-out) to `system-user.dtsi`, build,
then on the board `python3 ofdm_ctl.py selftest vectors/quick` / `play` / `live --chan 6`
(same CSI/STAT line format, so the dashboard's receiver panels work unchanged).

Off-board demo (any PC, no hardware): generate the demo vectors (`build_vivado.sh` step 1, or
`python3 csi_ref/make_vectors.py --out build/vectors/demo --preset demo` +
`hls/tb/golden_chain build/vectors/demo/iq80.bin build/vectors/demo $(cat build/vectors/demo/golden_args.txt)`),
then `python3 live/live_dashboard.py --source replay --dir build/vectors/demo` and open http://localhost:8080.

## Known issues / notes

* The xcvr1602 is ES1 → beta-device enablement (see Requirements).
* vrk160_base ships a static top wrapper: the overlay regenerates it so the RF pins
  reach the package (without it: `Not legal to attach pin ADC_CLK_P ... to GND`).
* RF clocking (tile PLL, 200 MHz reference) is a board assumption — see
  [`docs/RF_FRONTEND.md`](docs/RF_FRONTEND.md) before live RF.
* The fall alert fires ~13 s after the event (it must leave the 12.8 s AIE motion window).
* vrk160_base uses **segmented configuration**: without the PL + AIE image loaded
  (`fpgautil`, done by the boot service) any access to the PL control window
  (`0xA500_0000`) hangs the bus — run `host_wifi` only after it. (The plain design has
  one full PDI, configured by its BOOT.BIN.)
* Restarts are deterministic only with the host protocol (purge → epoch → 2048-word
  zero pre-roll, [OFDM_RX.md §7.1](docs/OFDM_RX.md)); `host_wifi` and `ofdm_ctl.py` do it.

### License
Apache License 2.0 — see [`LICENSE`](LICENSE).
