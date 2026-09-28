# Plan — WiFi-CSI Human Activity Recognition on the Versal RF VRK160

> **One line:** the VRK160 *is* the WiFi sensor — its RF-ADC samples the 2.4 GHz
> band directly, an 802.11 OFDM receiver in the PL extracts the Channel State
> Information (CSI) of every packet, the AI Engine turns the CSI time series into
> motion / breathing / phase features, and Linux on the A72 serves a live dashboard.

This is the VRK160 port of
[Wifi-HAR-Versal-VCK190-PL](../../Wifi-HAR-Versal-VCK190-PL) and keeps its technique
and conventions: HLS kernels + a pure-C AIE graph, **v++ co-generation with a
postSysLink overlay** for everything that touches the AI Engine, XRT-managed DDR
buffers for every PL→DDR path, a tracked PetaLinux `project-spec`, a stdlib-only
Python dashboard, and a bit-exact software golden model for every stage.

## 1. What changes from the VCK190 design

| | VCK190 (reference) | VRK160 (this repo) |
|---|---|---|
| CSI source | Raspberry Pi + nexmon_csi, UDP over SFP → PL UDP parser | **own 802.11 OFDM receiver** on the RF-ADC (no external CSI tool) |
| Device | xcvc1902 (AIE1 50×8), CIPS, DDR4 | xcvr1602 ES1 (AIE1 42×3), ps_wizard (A72/R5F), NoC2 + LPDDR5 |
| Platform | xilinx_vck190_base | vrk160_base (Vitis 2025.2) |
| PL front end | GT + MAC + UDP parser | RF-DC + wifi_ddc + ofdm_fe/ofdm_be + csi_feat |
| AIE graph | 3-branch FIR→stats / DFT / stats | **same kernels, unchanged** (re-targeted) |
| Link overlay | inserts csi_mux / Ethernet on the mm2s→AIE net | inserts the RF front end (iq_mux) on the mm2s_iq→wifi_ddc net |
| Live-path lesson reused | fixed-PA `/dev/mem` DMA Async-SErrors (#27) → XRT buffers (#28) | XRT buffers from day one (`s2mm_csi`, `s2mm_feat`, `mm2s_iq`) |

## 2. Architecture

```
              ┌──────────────────────── VRK160 (xcvr1602) ─────────────────────────┐
 antenna ─LNA─┤ RF-ADC 6.4 GSPS → DDC → 80 MSPS ─┐                                  │
              │                                   ├─ iq_mux ─ wifi_ddc ─ ofdm_fe ─ ofdm_be ─┬─ csi_feat ─ AIE ─ feat_pack ─ s2mm_feat ─┐
              │ DDR ─ mm2s_iq (playback) ─────────┘                                     └─ s2mm_csi ─────────────────────────────┤
              │                                                                                                              DDR
              │ A72 Linux: host_wifi (XRT) ── FEAT / CSI / STAT lines ── live_dashboard.py (HTTP :8080, SSE)
              └─────────────────────────────────────────────────────────────────────┘
```

* **Stage 1 — OFDM receiver** (PL, `hls/wifi_ddc`, `hls/ofdm_rx`): L-STF detection,
  L-LTF timing, CFO, 64-pt FFTs, channel estimate, L-SIG decode. Deliverable on its
  own as the plain-Vivado design (`hw/scripts/project_top.tcl`) with `sw/ofdm_ctl.py`.
* **Stage 2 — CSI capture → AI Engine** (`hls/csi_feat`, `aie/src`): per-packet CSI →
  normalised amplitude / sanitised phase time series → AIE windows; features and CSI
  records land in XRT buffers.
* **Stage 3 — dashboard** (`live/live_dashboard.py`): activity / presence / breathing /
  fall + CSI waterfall + receiver statistics, board-hosted.

## 3. Key design decisions

1. **Receive WiFi on the RF-ADC, not a companion device.** The converter's fine NCO
   and two DDC stages bring the band to 80 MSPS; the PL finishes channel selection
   (run-time NCO, ch1–11) and decimation to 20 MSPS where the filter can be made
   flat across the 52 used subcarriers.
2. **Legacy preamble only.** Every OFDM PPDU (a/g/n/ac/ax) starts with L-STF/L-LTF/
   L-SIG, so CSI from the L-LTF works for all modern traffic; L-SIG decode (Viterbi)
   validates real packets and rejects interference triggers. No DATA decoding.
3. **Split receiver**: a sample-rate front end (II = 1, integer) and a per-packet back
   end (float, ≤ 7.6 µs/packet) joined by a capture FIFO — the FE never needs to stall
   in real time; in playback, back-pressure makes the path lossless.
4. **Deterministic float** (IEEE basic ops, tables, polynomial atan2): the HLS C++ is
   itself the golden model — C-sim, RTL (co-sim) and the g++ build are bit-identical.
5. **Keep the AIE kernels** that were bit-accurate on VCK190 silicon; adapt the *inputs*
   instead (csi_feat window conditioning: zero-mean motion, detrended breathing,
   circularly-centred phase).
6. **v++ co-generation for the AIE** (VCK190 #21/#22): a plain-Vivado BD cannot
   produce the AIE↔PL shim solution; the RF front end is inserted by a postSysLink
   overlay, `ai_engine_0` is never touched. The overlay also regenerates the top
   wrapper (vrk160_base ships a static one — RF pins would be tied to ground).
7. **One RF front-end definition** (`rf_frontend.tcl`) shared by the v++ overlay and
   the plain design; one address map (`wifi_common.h`) shared by all software.
8. **Playback path in hardware** (mm2s_iq → iq_mux): the whole chain is testable on
   any board (farm, no antenna) against the golden vectors, and the dashboard demo
   runs without RF.
9. **Deterministic restart** (OFDM_RX.md §7.1): `epoch` only arms the kernels, which
   restart in-band at the first non-zero sample; a capture in flight is completed and
   flagged stale; the host purges queued records (s2mm idle exit) and plays a zero
   pre-roll. Any self-test after any previous traffic is bit-identical to a fresh one.
10. **Segmented configuration** (vrk160_base): BOOT.BIN brings up PS/NoC/DDR only; the
   PL + AIE graph image produced by `v++ -p` (`.pdi` + `.dtbo` with the zocl node) is
   loaded from Linux with `fpgautil` by the boot service — not baked into BOOT.BIN
   as on the VCK190.

## 4. Feature → activity mapping (dashboard)

Calibrated on the golden activity scenario (§ calibration in PROJECT_STATE.md):

| AIE feature (per 12.8 s window, hop 0.5 s) | empty | lying | breathing (standing) | walking |
|---|---|---|---|---|
| motion power (FIR 1–3 Hz → power) | 7.6e-7 | 5.5e-6 | 1.1e-5 | 2.8e-2 |
| phase variance | 0.008 | 0.007 | 0.29 | 0.60 |
| breathing peak (32 s window) | – | 18.8 bpm (18 injected) | 15.0 bpm (15 injected), SNR 73 | – |

Rules: active if motion > 3e-3; present if motion > 2e-6 or breathing SNR > 6;
fall = ≥ 5 s still → a burst > 5e-4 lasting ≤ 16 s → still again while present.

## 5. Out of scope / next steps

* Live RF validation needs a VRK160 with an antenna (+ LNA) on ADC tile 200 and a
  confirmed RF reference clock (docs/RF_FRONTEND.md) — the farm boards have neither.
* 40/80 MHz channels (HT40/VHT80 CSI) — needs 128/256-pt FFTs and a wider DDC.
* HT-LTF / multi-antenna CSI (MIMO) using more ADC slices.
* Learned classifier on the ruview 8-feature vector (`features8` is already emitted).
