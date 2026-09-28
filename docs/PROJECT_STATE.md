# PROJECT STATE — VRK160 WiFi-CSI Human Activity Recognition

> Portable handoff / build-log file (same role as the VCK190 repo's
> `docs/PROJECT_STATE.md`). §1 status, §2 environment, §3–§5 verified results,
> §6–§7 hardware / Linux builds, §8 every gotcha hit on the way, §9 next steps.

## 1. Status at a glance

| Item | State | Evidence |
|---|---|---|
| Python PHY golden model (TX, channel, float RX) | ✅ | 6/6 packets all 8 rates, CFO ±0.6 kHz, CSI 2–3 % (`csi_ref/wifi_ofdm.py`) |
| Bit-exact C++ golden chain (the HLS sources + AIE cores) | ✅ | `make -C hls/tb check` → `CHECK_GOLDEN PASS` |
| OFDM RX quality (demo scenario, 2 920 pkts, 30 dB) | ✅ | 100 % detected, 100 % L-SIG, RATE/LENGTH 100 %, CSI 2.1 %, T1 = Python RX 100 % |
| HLS C-sim (8 kernels) | ✅ | bit-exact vs golden stream dumps |
| HLS C/RTL co-sim | ✅ | **bit-exact**: wifi_ddc, ofdm_fe, ofdm_be, csi_feat, feat_pack, mm2s/s2mm |
| HLS timing @ 312.5 MHz | ✅ | ≤ 2.98 ns estimated (docs/OFDM_RX.md §8) |
| Deterministic restart (any prior traffic) | ✅ | `golden_chain --prefix`: 145 cut points (73 mid-capture) → byte-identical CSI + features |
| AIE graph on vrk160_base (AIE1 42×3) | ✅ | `aiecompiler` 0 errors (4 kernels, 2 tiles, 3+3 PLIO); x86sim = golden cores (max abs err 0, 3 branches) |
| Features → HAR (golden AIE outputs) | ✅ | motion walking/empty 36 000×, breathing 15.0 bpm exact, one fall alert, no false alarm |
| Dashboard (replay source) | ✅ | activity / breathing / fall / CSI waterfall verified in a browser |
| v++ link (AIE + kernels + RF overlay) → XSA, xclbin, run-time PDI/dtbo | ✅ | §6.3: WNS +0.214 ns, 8.9 % LUT |
| Plain-Vivado OFDM RX design (Tcl) → XSA | ✅ | §6.3: WNS +0.221 ns, 6.2 % LUT |
| PetaLinux project + SDK + SD image | ✅ | §7 (`build_petalinux.sh`, BOOT.BIN + boot.scr + image.ub) |
| On silicon | ⬜ | farm VRK160s exist (`anvil target status vrk160`) — not attempted in this session |

## 2. Environment (host `xcoapps75`, Ubuntu 24.04)

```bash
source /proj/gsd/vivado/2025.2/Vitis/settings64.sh        # Vivado/Vitis/HLS/aiecompiler/v++ 2025.2
PLATFORM=/proj/gsd/vivado/2025.2/Vitis/base_platforms/vrk160_base/vrk160_base.xpfm
python3 -m venv /tmp/venv_vrk && /tmp/venv_vrk/bin/pip install numpy scipy   # golden model
export TMPDIR=/tmp/petalinux-$USER                                          # before PetaLinux
source /proj/petalinux/2025.2/petalinux-v2025.2_11160223/tool/petalinux-v2025.2-final/settings.sh
```
* Part `xcvr1602-vsva2488-2MP-e-S-es1`, board `xilinx.com:vrk160:part0:1.1`.
* vrk160_base: A72 + R5F (ps_wizard), 4× LPDDR5 (NoC2), AIE1 42 columns × 3 rows,
  clocks 625 / **312.5 (default, kernels)** / 156.25 / 78.1 / 100 MHz, SP tags LPDDR, LPDDR0-3.
* No VRK160 PetaLinux BSP in 2025.2 → project from `--template versal` + SDT (machine `xlnx-vrk160`).

## 3. Golden model and receiver results

`csi_ref/make_vectors.py` builds playback vectors at the RF-DC DDC output format
(80 MSPS int16 I/Q): `quick` = 40 packets, +5 MHz offset (exercises the PL NCO),
25 dB, 61 kHz CFO; `demo` = 146 s activity scenario (empty 20 s, walking 20 s,
breathing 50 s, fall 1 s, lying 40 s, walking 15 s) at 20 packets/s, 30 dB, 37 kHz CFO.
`hls/tb/golden_chain` runs the HLS sources + AIE cores; `csi_ref/check_golden.py`
cross-checks against ground truth and the independent Python receiver.

| | quick | demo |
|---|---|---|
| packets detected / L-SIG valid | 40 / 40 | 2 920 / 2 920 |
| RATE / LENGTH decoded correctly | 100 % / 100 % | 100 % / 100 % |
| CFO estimate | 60 957 ± 352 Hz (61 000) | 37 284 ± 496 Hz (37 000) |
| \|CSI\| vs truth (median, gain-normalised) | 2.7 % | 2.1 % |
| T1 vs Python RX | 100 % | 100 % |
| CSI HW-model vs Python RX (complex) | 8·10⁻⁴ | 8·10⁻⁴ |

## 4. Feature calibration (AIE outputs of the golden demo, pure windows)

| | empty | lying | breathing | walking |
|---|---|---|---|---|
| motion power | 7.6e-7 | 5.5e-6 | 1.1e-5 | 2.75e-2 |
| phase variance | 0.0078 | 0.0072 | 0.29 | 0.60 |
| breathing peak (32 s window) | – | 18.8 bpm (18 inj.), SNR 12.5 | **15.0 bpm (15 inj.)**, SNR 73 | – |

Fall signature: long stillness → ~1.7e-3 motion plateau lasting exactly one 12.8 s
window → stillness (lying). Walking → still transitions have long > 3e-3 bursts.
Dashboard (`live_dashboard.py`): active > 3e-3, present > 2e-6 or breathing SNR > 6,
fall = ≥ 10 still windows → burst > 5e-4 for ≤ 32 windows → ≥ 3 still windows while
present. Offline run of the dashboard logic over the demo: empty 12.8–20 s, walking
from 23.8 s, still-breathing from 68 s, **one FALL alert at 104.8 s** (fall at 90 s;
≈ 13 s window latency), no false alert.

## 5. HLS

`hls/build_kernels.sh` (parallel, `run_hls.tcl`, `enable_beta_device *` before
`set_part`, `config_op facc|fmacc -impl none`) → `hls/build/xo/*.xo` + `hw/ip_repo/*`
(≈ 2 min for all 8). Numbers in docs/OFDM_RX.md §8.
Co-sim: `FLOW=csynth,cosim hls/build_kernels.sh <k>` (switches the per-record
free-running kernels to `ap_ctrl_hs` via `HLS_COSIM_HS`; datapath RTL unchanged). All
kernels bit-exact against the golden stream dumps: wifi_ddc (per-sample, quick
vectors), ofdm_fe (14 captures), ofdm_be (12 packets), csi_feat (5 windows, 1 280 +
320 + 1 280 window words + tags), feat_pack, mm2s → s2mm (blocking and idle-exit runs).

## 6. Hardware builds

### 6.1 v++ co-generation (the AIE design) — `aie/link.sh`
* post-SysLink BD of vrk160_base is **flat**: control `/ctrl_smc` (aclk 156.25, aclk1
  312.5 MHz), kernel clock net `clk_wizard_0_clk_out1_o2`, reset
  `/proc_sys_reset_o4/peripheral_aresetn`, DDR via `/aggr_noc` S*_AXI, AIE PLIO
  `ai_engine_0/S0x_AXIS`/`M0x_AXIS`.
* `overlay_rf.tcl` inserts the RF hierarchy on `mm2s_iq/s → wifi_ddc/iq_in`, widens
  `ctrl_smc` by 2 masters + a 100 MHz clock, pins all addresses, regenerates the top wrapper.
* `RF_VALIDATE_ONLY=1 ./link.sh` — structural check in ~3 min (validate_bd_design PASS).
* `package.sh`: the linked XSA is a *segmented configuration* → it is passed to
  `v++ -p` as the **platform** (gotcha #23) → xclbin + run-time PDI/dtbo.
* Result: see §6.3.

### 6.2 Plain Vivado (the OFDM-receiver design) — `hw/scripts/project_top.tcl`
* `vrk160_ofdm_design.tcl` = captured vrk160_base root design (PFM removed, NoC/PS TLM
  sim models → RTL) + `add_ofdm_rx`; `bd_only` validates (only tie-off:
  `axi_intc_0/intr`, unused — the plain design polls).
* Result: see §6.3.

### 6.3 Results (Vivado/Vitis 2025.2, xcvr1602-vsva2488-2MP-e-S-es1)

| | v++ co-generated WiFi-HAR design (`aie/link.sh`) | plain OFDM-receiver design (`project_top.tcl`) |
|---|---|---|
| Output | `aie/wifi_har_vrk160.xsa` (8.0 MB, segmented: boot + PLD PDI) → `package.sh` → `.xclbin` + `package/wifi_har_vrk160.{pdi,dtbo}` | `hw/build/vrk160_ofdm_rx.xsa` (6.7 MB, one full PDI) |
| Timing @ 312.5 MHz kernels, 80 MHz RF | **WNS +0.214 ns, WHS +0.012 ns** — all constraints met | **WNS +0.221 ns, WHS +0.019 ns** — all constraints met |
| CLB LUT / FF | 49 151 (8.9 %) / 60 639 (5.5 %) | 33 974 (6.2 %) / 41 134 (3.7 %) |
| BRAM / URAM / DSP | 49 (4.4 %) / 0 / 154 (6.8 %; 65 DSPFP32) | 36.5 (3.3 %) / 0 / 124 (5.5 %) |
| AI Engine | 4 kernels on 2 tiles (FIR+stats, DFT, phase stats), 3 PLIO in / 3 out | — |
| Build time (96-core host) | link ≈ 42 min, package 30 s | ≈ 36 min |

Per-kernel post-route utilisation (v++ design): csi_feat 12.5 k LUT / 30 DSP / 10 BRAM,
ofdm_be 13.4 k / 38 / 14, ofdm_fe 7.3 k / 20 / 0, wifi_ddc 2.3 k / 60 / 0, s2mm ×2 2.9 k,
mm2s 1.2 k, feat_pack 0.4 k, iq_mux 0.2 k. Full reports: [`docs/reports/`](reports/)
(`vpp_link_*`, `plain_ofdm_rx_*`). The integrated BD as v++ + the overlay built it is
captured in `hw/scripts/vrk160_wifi_har_bd.tcl` (written by the overlay at every link);
`hw/scripts/recreate_wifi_har_bd.tcl` re-creates it in a plain Vivado project (sim models
→ RTL, mover apertures assigned) and passes `validate_bd_design` (37 cells, ≈ 1 min).

## 7. PetaLinux (`petalinux/`)
* Created with `petalinux-create project --template versal --name petalinux`;
  hardware via `petalinux/scripts/import_hw.sh <xsa>` (sdtgen → `petalinux-config
  --get-hw-description <sdt>` → base PDI pinned to the XSA's `*_boot.pdi`). rootfs adds
  xrt, xrt-dev, zocl, ai-engine-driver, python3 (+modules), bash, util-linux, ethtool,
  openssh, fpga-manager-script (`fpgautil`), libdfx, and the `wifi-har` recipe
  (host_wifi, xclbin, PL/AIE `.pdi` + `.dtbo`, dashboard, vectors, boot service).
* **Segmented configuration** (vrk160_base): `BOOT.BIN` = boot PDI (PMC/LPD/FPD, NoC,
  DDR, AIE array init) + PLM/PSM + dtb + BL31 + U-Boot. The PL and the AIE graph are
  loaded at run time from the v++ package output: `fpgautil -b wifi_har_vrk160.pdi -o
  wifi_har_vrk160.dtbo` (PLD PDI + `aie_image` CDO; the overlay adds the PL nodes, the
  AI Engine node and the **zocl** node). PetaLinux therefore runs with
  `CONFIG_SUBSYSTEM_FPGA_MANAGER=y` + "full" PL overlay type: the base DT has no PL
  nodes and is compiled with `-@` (labels `amba`, `fpga`, `gic`, `versal_firmware`
  resolved by the overlay).
* `build_petalinux.sh`: import → SDK → cross-compile the host → stage the recipe →
  final image → `petalinux/scripts/build_boot.sh` (BOOT.BIN from the XSA's boot PDI) →
  `images/linux/sd/`.
* **Result** (2025.2, kernel 6.12.40-xilinx, XRT 2.20): `sd/BOOT.BIN` 3.6 MB (images
  pmc_subsys, lpd, pl_cfi (boot segment), aie_subsys, fpd, apu_subsystem), `boot.scr`,
  `image.ub` 236 MB (Image + dtb + initrd). Checked offline: the v++ overlay applies
  cleanly on the PetaLinux `system.dtb` (`fdtoverlay` → zocl, AIE and all PL nodes,
  `firmware-name = "wifi_har_vrk160.pdi"`); the initrd holds `/home/root/wifi-har/`
  (host_wifi, xclbin, pdi, dtbo, dashboard, vectors — md5 = build outputs),
  `/usr/bin/fpgautil`, `zocl.ko`, `libxrt_coreutil`, python3 and the enabled
  `wifi-har.service`. Full flow ≈ 25 min with a warm sstate (`SKIP_SDK=1` ≈ 13 min).

## 8. Gotchas (each cost an iteration)

1. **ES1 part**: Vivado needs `enable_beta_device` (user's Vivado_init.tcl); HLS needs it
   *before* `set_part`, so `v++ -c`'s `--hls.pre_tcl` is too late (runs after set_part) →
   kernels are exported to `.xo` by `vitis-run --mode hls` + `export_design -format xo`.
2. `set -u` + sourcing `settings64.sh` silently exits scripts → source first, then `set -u`.
3. HLS renames ports that are HDL keywords (`in`→`in_r`, `out`→`out_r`) → all stream
   ports have explicit names (`iq_in`, `iq_out`, `csi_in`, `feat_out`).
4. v++ refuses kernels with unconnected stream ports ("No stream resources … dmux.rf")
   → the RF source cannot be a dangling kernel port; the overlay inserts `iq_mux` on an
   existing net (the VCK190 `csi_mux` pattern).
5. Vitis-kernel mode forbids `ap_none` ports → `iq_mux` (needs the `rf_resetn` wire) is
   built with `-flow_target vivado` and only used as an IP.
6. **Schmidl-Cox edge trigger**: normalising `|C|` by the current window power alone
   triggers at the end of every packet (window slides from packet into noise:
   `|C|/P ≈ √(SNR/48)`) → 22/1016 false packets. Fixed with `|C|² ≥ thr²·P·Pd`.
7. **AIE FIR start-up**: the (reused) FIR is causal with zero history; a signal near 1.0
   made a step transient that dominated the motion power (the VCK190 demo's ~2.7e-3
   "still floor"). csi_feat sends zero-mean windows → still floor 7.6e-7.
8. Breathing leakage from EMA drift → breathing window mean + linear detrend; phase
   window centred on its circular mean (no ±π wraps).
9. Fall rule "spike then stillness" fires on every walking→standing transition → burst
   duration / prior-stillness state machine.
10. ofdm_fe first missed 312.5 MHz (4.2 ns: accumulator → sitofp chain + control
    recurrence) → metric from registered sums + flattened control (2.66 ns), captures
    bit-identical (verified on 2 060 packets).
11. ofdm_be float max recurrence (3.26 ns) → integer compare of IEEE bits (2.58 ns).
12. Co-sim only supports `ap_ctrl_none` if II=1 (or combinational) → `HLS_COSIM_HS`
    for the per-record kernels (ofdm_be, csi_feat); free-running RTL may run before the
    TB's `epoch` write → FE TB compares timestamps relative.
13. **vrk160_base ships a static imported top wrapper** → overlay-added RF ports were
    tied to GND: `Not legal to attach pin ADC_CLK_P of site RFADC_X0Y0 … to GND` →
    overlay regenerates the wrapper (`make_wrapper`) and swaps it into `sources_1`.
14. **RF-DC 1.2 PVT_SAS**: the IP instantiates two `PVT_SAS` temperature readouts; the
    xcvr1602 ES1 device model has no such site (`UTLZ-1 PVT_SAS over-utilized`) →
    `hw/scripts/fix_pvt_sas.tcl` (impl opt_design pre-hook) removes them and ties the
    temperature to 0 (the IP's own commented fallback). 2026.1 (v1.3) uses SYSMON satellites.
15. `clkx5_wiz` exposes `resetn`/`locked` only with `USE_RESET`/`USE_LOCKED`; its
    frequency list is comma-separated.
16. Captured platform BD mixes TLM/RTL NoC sim models → `validate_bd_design` error
    outside the platform → set all to RTL in the captured Tcl.
17. PetaLinux SDT flow needs a PDI in the SDT dir; the extensible vrk160_base XSA has
    none. A placeholder PDI makes `bootgen` spin forever in `xilinx-bootbin` → always
    import the fixed (post-impl) XSA before building the boot image.
18. **Restart determinism.** A plain "reset everything on `epoch`" is not enough for a
    free-running chain: the register write lands at an arbitrary point (mid-capture,
    mid-decimation phase, records queued in FIFOs), and a kernel stalled on a stream
    samples the new value one sample late. Solution (docs/OFDM_RX.md §7.1): `epoch` only
    *arms*; `wifi_ddc`/`ofdm_fe` restart at the first non-zero sample; an in-flight
    capture completes flagged `CAP_STALE` and `ofdm_be` drops it; a 2048-word zero
    pre-roll; `s2mm` gained `idle_max` + a word count so the host can purge queued
    records and never leaves a run pending. Proven on the model: 145 prefix cut points
    (73 mid-capture) → byte-identical outputs (`golden_chain --prefix`).
19. Resetting `ofdm_fe`'s detector sums together with its control state put the float
    metric back on the 1-cycle recurrence (6.7 ns estimated, and co-sim refused the
    design as not II=1) → only the control state restarts; the exact-integer detector
    state is flushed to 0 by the pre-roll (2.98 ns).
20. **HLS inferred DSPFP32 accumulators** (`facc`/`fmacc`, 'standard' precision) round
    a running float sum differently from the sequential C loop → csi_feat windows a few
    ULP off in co-sim → `config_op facc|fmacc -impl none` in `run_hls.tcl` (fadd on
    DSPFP32 still closes II=1 at 312.5 MHz); `fmadd`/`fmsub` primitives are exact.
21. `aiecompiler --target=x86sim` also writes `libadf.a` → an x86sim run silently
    replaced the hw archive and `v++ -l` failed ("generated with x86sim target") → the
    x86sim build now uses `--output-archive=libadf_x86.a`.
22. PetaLinux `settings.sh` reads `$1` as its install directory: sourced inside a
    script/function it picked up the caller's arguments (XSA path / command) and
    silently did nothing → `set --` before sourcing. It also needs a local,
    whitespace-free `TMPDIR` (host-tool checks compile in it and otherwise report
    "ncurses missing"); the scripts force `/tmp/petalinux-$USER`.
23. **vrk160_base uses Versal segmented configuration** (linked XSA = `*_boot.pdi` +
    `*_pld.pdi`, no full PDI): `v++ -p` refuses the XSA as an input file ("contains a
    segmented configuration. Please specify it as a platform option") → package with
    `--platform <linked.xsa>`, which yields the xclbin + `<name>.pdi` (PLD + aie_image) +
    `<name>.dtbo`. The xclbin header has action mask 0 → XRT/zocl does **not** load its
    PDI section (only for `AM_LOAD_PDI`/PR xclbins, see zocl_edge_xclbin.c) → the PL +
    AIE image is loaded with `fpgautil` before `host_wifi` (the VCK190 recipe of baking
    the AIE CDO into BOOT.BIN does not apply).
24. PetaLinux kept `PDI_PATH = …/placeholder.pdi` in `build/conf/machine/*.conf` from
    the very first (placeholder) import although later imports had real PDIs →
    `base-pdi: No base pdi supplied` → `import_hw.sh` pins `PDI_PATH` to the XSA's
    `*_boot.pdi` (gen-machineconf's own rule) after every import.
25. meta-xilinx `device-tree` `do_configure:append` *appends* `#include
    "system-user.dtsi"` to the DTS in its work dir; after a re-import it re-ran on the
    already-patched copy → included twice → `system-conf.dtsi: Duplicate node name
    …/flash@0/partition@0` → `build_petalinux.sh` does `-c device-tree -x cleansstate`
    right after the import.
26. Cross-linking `host_wifi` against the 2025.2 SDK: `undefined reference to
    'uuid_clear'` / "DSO missing from command line" — `xrt::uuid` is header-inline
    libuuid → link with `-lxrt_coreutil -luuid -pthread`.

## 9. How to resume / next steps
```bash
make -C hls/tb check PY=/tmp/venv_vrk/bin/python     # golden sanity (seconds)
PY=/tmp/venv_vrk/bin/python ./build_vivado.sh        # vectors -> HLS -> AIE -> link -> package (~50 min)
PLAIN=1 PY=/tmp/venv_vrk/bin/python ./build_vivado.sh  # + the plain OFDM-RX design (~35 min more)
./build_petalinux.sh                                 # PetaLinux -> petalinux/images/linux/sd/
```
On a board (SD boot, or JTAG: `BOARD_URL=TCP:<host>:3121 xsdb petalinux/scripts/boot_jtag.tcl`
then `booti 0x00200000 0x04000000 0x00001000`), the `wifi-har` service loads the PL + AIE
image and runs the self-test; by hand in `/home/root/wifi-har`:
```sh
fpgautil -b wifi_har_vrk160.pdi -o wifi_har_vrk160.dtbo    # once per boot (service does it)
./host_wifi selftest wifi_har_vrk160.xclbin vectors/quick  # -> SELFTEST PASS (bit-exact CSI)
./host_wifi selftest wifi_har_vrk160.xclbin vectors/demo   # 2 920 packets + 267 AIE feature records
./host_wifi play wifi_har_vrk160.xclbin vectors/demo --loop | head
./host_wifi live wifi_har_vrk160.xclbin --chan 6           # antenna on ADC tile 200 + RF clock (RF_FRONTEND.md)
```
Open items: first on-silicon run (farm VRK160s: `anvil target status vrk160`), then
live RF (antenna/LNA, reference-clock confirmation), see plan.md §5.
