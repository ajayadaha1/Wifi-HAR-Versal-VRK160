# OFDM receiver, CSI and feature pipeline (PL) — design reference

This document specifies every PL kernel of the VRK160 design: the algorithms,
number formats, stream/record formats and register maps. The source of truth for
the constants is [`hls/common/wifi_common.h`](../hls/common/wifi_common.h) and the
generated tables [`hls/common/wifi_tables.h`](../hls/common/wifi_tables.h) /
[`hls/common/ddc_taps.h`](../hls/common/ddc_taps.h) (both produced by
[`csi_ref/wifi_ofdm.py`](../csi_ref/wifi_ofdm.py) from the same definitions the
golden model uses).

```
RF-ADC tile 200 (6.4 GSPS, fine NCO, DDC /2 /40)
   │ 80 MSPS I/Q, 1 sample / 80 MHz clk
   ▼ async FIFO (80 MHz → 312.5 MHz)
iq_mux ◄── mm2s_iq (DDR playback)          all kernels @ 312.5 MHz
   ▼
wifi_ddc   PL NCO (ch1..ch11) + 64-tap FIR, /4  → 20 MSPS
   ▼
ofdm_fe    L-STF detector + L-LTF timing, capture (II=1, per sample)
   ▼ capture: 416 samples + 8-word trailer   (FIFO 1024)
ofdm_be    CFO, 3× FFT-64, channel estimate (CSI), SNR, L-SIG Viterbi
   ├─► csi  ─► csi_feat ─► AIE {motion, breathing, phase-var} ─► feat_pack ─► s2mm_feat ─► DDR
   └─► rec  ─► s2mm_csi ─► DDR   (CSI records for the host / dashboard)
```

## 1. Sample format and rates

| Point | Rate | Format |
|---|---|---|
| RF-DC `m00_axis` → `iq_mux.rf` | 80 MSPS (80 MHz fabric clock) | 32-bit `{Q[31:16], I[15:0]}`, int16 |
| `mm2s_iq` → `iq_mux.pb` | up to 312 MSPS (playback, back-pressured) | same |
| `wifi_ddc.iq_out` → `ofdm_fe` | 20 MSPS (802.11 20 MHz channel) | same |

A 1 s real-time stream at 80 MSPS is 320 MB, so playback vectors store packets
back-to-back (200 idle samples between packets) and the CSI conditioner bins by
packet in playback mode (§6).

## 2. `wifi_ddc` — channel selection and decimation

* **NCO**: 32-bit phase accumulator, 1024-point Q1.15 cosine table (`COS1K`),
  `y = x · exp(+j·2π·phase/2³²)`, `phase += nco_inc` per 80 MSPS sample.
  `nco_inc = round(−f_off / 80 MHz · 2³²)` moves a signal at `+f_off` to DC
  (`wifi_ofdm.nco_inc_for()`, `host_wifi live --chan N`). With the RF-DC NCO at
  channel 6 (2437 MHz) the PL NCO reaches channels 1…11 (±25 MHz).
* **FIR**: 64 taps, symmetric, Q1.17 (`ddc_taps.h`, Parks-McClellan): pass
  0–8.6 MHz (ripple 0.42 dB), stop ≥ 11.4 MHz (−53.7 dB). 802.11 occupies ±8.3 MHz.
  Accumulator 50 bit, round-half-up, saturate to int16. Decimation by 4.
* Free-running, `PIPELINE II=1`: one 80 MSPS input per clock, one output every 4th
  input (decimation-phase counter), 60 DSP.
* `epoch`: a change **arms** a restart; NCO phase, delay line and decimation phase are
  reset at the first non-zero input sample, and the output is forced to 0 while armed
  (see §7.1).

## 3. `ofdm_fe` — packet detection and timing (per sample, II = 1)

Registers: `thr2` (f32, default 0.49 = 0.7²), `p_min` (f32, default 0), `n_plat`
(default 24), `holdoff` (default 480, ≥ 424 enforced), `epoch` (restart, §7.1);
status `n_trig`, `p_now`.

1. **L-STF detector** (Schmidl–Cox, delay 16, window 48), exact integer running sums:
   `C = Σ x[n]·x*[n−16]`, `P = Σ|x[n]|²`, `Pd = Σ|x[n−16]|²`.
   A sample is "high" when `|C|² ≥ thr2 · P · Pd` **and** `P > p_min`.
   The metric is a correlation coefficient (≤ 1 by Cauchy–Schwarz). *Normalising by
   `P` alone* (a common textbook form) triggers falsely at the end of every packet:
   when the window slides from the packet tail into noise, `|C|/P ≈ √(SNR/48) ≫ 1`
   — this was observed in the golden model (22/1016 false packets) and fixed.
2. Trigger after `n_plat` consecutive high samples (hold-off `holdoff` samples).
3. **L-LTF timing** (every sample, integer, CFO-robust): the 64-sample L-LTF period
   `T` is sign-quantised, `Tq = sgn(Re T) + j·sgn(Im T)` (`LTF_NEG_RE/IM`), and
   correlated in four 16-sample segments combined non-coherently,
   `M(p) = Σ_{s<4} |Σ_{i<16} x[p+16s+i]·Tq*[16s+i]|²`, so a CFO of up to ±230 kHz
   (5.8 GHz, ±40 ppm) costs little. `M2(p) = M(p) + M(p+64)` peaks at T1 (T2 = T1 + 64).
4. **Capture**: from `CAP_PRE = 32` samples before the trigger, `N_CAP = 416` samples,
   then an 8-word trailer: `{0xCA970001, T1, C.re, C.im, P, ts_lo, ts_hi, seq}`, where
   `T1 = argmax M2` over capture positions `[40, 200]` and `ts` = 20 MSPS sample index
   of capture word 0. The metric is evaluated from the *registered* sums of the
   previous sample (keeps `sitofp` off the 1-cycle recurrence at 312.5 MHz); the
   capture tap/offset are shifted by one to compensate — captures are bit-identical
   to the reference definition (verified on 2 060 packets).
5. **Restart** (`epoch` change, §7.1): no new triggers while armed; a capture in flight
   still completes (the frame towards `ofdm_be` is never cut) but its trailer `seq`
   word gets bit 31 (`CAP_STALE`); at the first non-zero sample with no capture in
   flight the control state (`seq`, timestamp, plateau counter, hold-off) restarts.
   The detector state needs no reset — its delay lines and running sums are exact
   integer recurrences that the zero pre-roll flushes to exactly 0 — which keeps the
   float metric out of the 1-cycle control recurrence (2.98 ns at 312.5 MHz).

The FE writes the capture with blocking writes. It never stalls in RF mode because
the back end needs ≤ 2 366 cycles (7.6 µs) per packet while the hold-off guarantees
≥ 480 samples (24 µs) between captures; in playback the back-pressure simply slows
the DDR playback (lossless).

## 4. `ofdm_be` — CFO, FFT, CSI, L-SIG (one packet per call)

Registers: `ctrl` [0] `rec_en` (copy records to `s2mm_csi`, must then be drained),
[1] `rec_sig_only`, [7:4] FFT window advance `adv` (0 → 2); `epoch`;
status `n_pkt`, `n_sig_ok`, `last_sig`, `n_resync`.

1. Framing check (TLAST exactly on trailer word 7, magic) — on error drop to the next
   TLAST (`n_resync`). Captures flagged `CAP_STALE` (completed by `ofdm_fe` while armed
   for a restart) are dropped: no record, not counted. The counters restart at the
   first capture with front-end `seq` 0 (or an `epoch` change).
2. Coarse CFO `ω_c = ∠C / 16`; fine CFO from the two LTF periods:
   `R = Σ_{i<64} y[T1+64+i]·y*[T1+i]`, `ω = ω_c + wrap(∠R − 64ω_c) / 64`.
3. De-rotation `y·e^{−jω(n−T1)}` (4096-point table, 1.5 mrad steps) of T1, T2 and
   L-SIG, FFT windows started `adv` samples inside the cyclic prefix.
4. Three 64-point radix-2 DIT FFTs (float, II = 2 butterflies).
5. **CSI**: `H[k] = (Y1[k] + Y2[k]) / 2 · L[k] / 64` on the 52 used subcarriers
   (FFT bin order, nulls = 0). Noise `σ² = Σ|Y1−Y2|² / (2·52·64²)`, SNR = mean|H|²/σ².
6. **L-SIG**: `Z = Ys · conj(64·H)`, common phase from the 4 pilots, BPSK soft bits
   of the 48 data subcarriers, de-interleaved (`INTLV`), quantised to 6 bits, K = 7
   r = 1/2 (133, 171)₈ Viterbi (64 states unrolled, tail-terminated) → RATE, LENGTH,
   parity, tail. `sig_ok` = parity ∧ tail = 0 ∧ reserved = 0 ∧ R4 = 1 ∧ LENGTH > 0.
7. Emit one CSI record to `csi` (always) and to `rec` (if enabled).

All arithmetic uses IEEE basic operations plus `sqrtf` (correctly rounded in both
HLS and glibc) and tables; `atan2` is a 9th-order polynomial (`hls/common/fmath.h`).
Therefore C-simulation, RTL and the host-side golden model are bit-identical.

### CSI record (144 × 32-bit words, `REC_MAGIC 0xC51F0001`)

| Word | Content |
|---|---|
| 0 | magic `0xC51F0001` |
| 1 | packet sequence number (`ofdm_be`) |
| 2–3 | timestamp: 20 MSPS sample index of T1 (lo, hi) |
| 4 | [23:0] decoded L-SIG bits, [24] sig_ok, [25] parity_ok, [26] tail_ok |
| 5 | CFO, Hz (f32) |
| 6 | mean L-STF power, LSB² (f32) → dBFS = 10·log10(p / 32768²) |
| 7 | SNR, linear (f32) |
| 8 | T1 (capture coordinates) |
| 9 | L-STF metric \|C\|/P at trigger (f32) |
| 10 / 11 | noise power / mean \|H\|² (f32) |
| 12 | [7:0] 64 subcarriers, [15:8] 0 = float32 I/Q, [23:16] rate Mb/s |
| 13 | L-SIG LENGTH (bytes) |
| 14 | front-end capture sequence number |
| 16–143 | H[k].re, H[k].im (f32), FFT bins k = 0…63 |

## 5. Validation of the receiver

| Check | Result |
|---|---|
| Golden model vs Python float RX (`csi_ref/check_golden.py`, 2 920 packets) | T1 100 % identical, CSI rel. diff 8·10⁻⁴ (table vs exact rotator) |
| Detection / L-SIG (demo scenario, 30 dB) | 2 920/2 920 detected, 2 920 L-SIG valid, RATE/LENGTH 100 % |
| CSI vs ground-truth channel (amplitude, gain-normalised) | median 2.1 % (30 dB), 2.7 % (25 dB, +5 MHz offset through the PL NCO) |
| CFO (61 kHz / 37 kHz injected) | 60.96 kHz ± 0.35 / 37.3 kHz ± 0.5 |
| HLS C-sim vs golden streams | bit-exact, all kernels |
| C/RTL co-simulation | bit-exact: wifi_ddc (32 768 inputs → 8 192 samples), ofdm_fe (14 captures, 5 936 words), ofdm_be (12 packets), csi_feat (5 windows: 1 280 + 320 + 1 280 words + tags), feat_pack, mm2s → s2mm (blocking and idle-exit runs) |
| Restart determinism (`golden_chain --prefix`, §7.1) | 145 cut points of prior traffic (73 leave `ofdm_fe` mid-capture) → CSI records, windows and features byte-identical to a clean run |

## 6. `csi_feat` — CSI → AI Engine branch inputs

Registers: `ctrl` [0] packet bins (playback) / time bins (live), [1] use L-SIG-valid
packets only; `bin_us` (50 000 → 20 Hz), `hop` (10), `br_dec` (10), `k_ref` (13),
`fs_nom` (20.0), `alpha` (EMA, 1/256), `epoch`; status `n_in`, `n_win`, `n_samp`.

Per packet: `amp = mean_k |H[k]|`, `a_rel = amp / EMA(amp) − 1` (gain-invariant);
linearly-sanitised phase `φ = ∠(H[k_ref]·e^{−j(a·k_ref+b)})` with slope
`a = ∠Σ H[k+1]H*[k]` and offset `b = ∠Σ H[k]e^{−jak}` (removes CFO / timing).
Samples are per packet (playback) or per time bin (live: bins averaged, empty bins
hold). All history (windows, EMA, bins, counters) restarts at the first record with
front-end `seq` 0 or an `epoch` change. Every `hop` samples (after 256 samples) three
windows are sent:

| Branch | Window | Conditioning | AIE kernel |
|---|---|---|---|
| motion | 256 × a_rel @ fs | minus mean (the AIE FIR is causal with zero history: an offset would add a start-up transient) | FIR 1–3 Hz → stats |
| breathing | 64 × a_rel averaged by `br_dec` (2 Hz, 32 s) | minus mean and linear trend | Hann DFT magnitude, 33 bins |
| phase-var | 256 × φ | centred on the circular mean (no ±π wrap) | stats (variance) |

plus a 12-word tag `{0xFEA70001, win_seq, ts_lo, ts_hi, n_pkts | brt_full<<31,
rssi, snr, fs, Σmot, Σbrt, Σphs, cfo}` to `feat_pack`.

`feat_pack` merges tag + AIE outputs into a **52-word feature record**
(`[0..11]` tag, `[12..14]` motion {mean, var, power}, `[15..47]` breathing bins,
`[48..50]` phase {mean, var, power}, `[51]` framing flag) for `s2mm_feat`.

## 7. Register map (AXI-Lite, identical in the v++ and the plain design)

| Base | Block | Registers (offset) |
|---|---|---|
| `0xA500_0000` | `iq_mux` | `sel` 0x10 ([0] playback, [1] drain RF, [2] RF-domain reset release), `rf_count` 0x18 (RO) |
| `0xA501_0000` | `wifi_ddc` | `nco_inc` 0x10, `ctrl` 0x18 ([0] swap IQ, [1] conj, [2] bypass NCO, [10:8] gain), `epoch` 0x20 |
| `0xA502_0000` | `ofdm_fe` | `thr2` 0x10, `p_min` 0x18, `n_plat` 0x20, `holdoff` 0x28, `epoch` 0x30, `n_trig` 0x38 (RO), `p_now` 0x48 (RO) |
| `0xA503_0000` | `ofdm_be` | `ctrl` 0x10, `epoch` 0x18, `n_pkt` 0x20, `n_sig_ok` 0x30, `last_sig` 0x40, `n_resync` 0x50 (RO) |
| `0xA504_0000` | `csi_feat` | `ctrl` 0x10, `bin_us` 0x18, `hop` 0x20, `br_dec` 0x28, `k_ref` 0x30, `fs_nom` 0x38, `alpha` 0x40, `epoch` 0x48, `n_in` 0x50, `n_win` 0x60, `n_samp` 0x70 (RO) |
| `0xA520_0000` | `rfdc_0` | Versal RF data converter (2 MB) |
| v++-assigned | `mm2s_iq`, `s2mm_csi`, `s2mm_feat` | XRT kernels (`ap_ctrl_chain`): `mem` 0x10, `size` 0x1C, s2mm `idle_max` 0x24 |
| `0xA505_0000` / `0xA506_0000` | `mm2s_iq` / `s2mm_csi` | plain design only (same register layout) |

**Data movers.** `mm2s(mem, size)` streams `size` words (TLAST on the last).
`s2mm(mem, size, idle_max)` stores up to `size` words; with `idle_max > 0` it also
returns after `idle_max` consecutive clock cycles without data, and it always writes
the number of received words to `mem[size]` (buffers are `size + 1` words). No run is
ever left pending (host exit, purge, timeouts) and the host re-frames on the record
magic, so a record split across two runs is harmless.

### 7.1 Restart protocol (deterministic self-test after any previous traffic)

A streaming chain that was stopped mid-packet (host killed, live RF switched off,
playback cut) holds state everywhere. The host brings it back to a state that is
**bit-identical to a freshly configured design** in three steps:

1. **Purge** (`host_wifi`, `ofdm_ctl.py`): park the RF input (`iq_mux.sel` = playback
   + drain RF), then run `s2mm_csi`/`s2mm_feat` with `idle_max` = 50 ms until they
   return short — everything already framed downstream of `ofdm_fe` is discarded.
2. **Epoch**: write `last + 1` to every `epoch` register. This only *arms* the kernels.
3. **Zero pre-roll**: every playback starts with `PREROLL_WORDS = 2048` zero words
   (= 512 samples at 20 MSPS ≥ one capture of 424 words + margin). A capture that was
   in flight in `ofdm_fe` completes inside the pre-roll, flagged `CAP_STALE`, and
   `ofdm_be` drops it; the detector state flushes to exactly 0; `wifi_ddc` and
   `ofdm_fe` restart at the first non-zero sample of the new data; `ofdm_be` and
   `csi_feat` restart on the first record with `seq` 0.

`golden_chain --prefix <other.iq80> [--prefix-cut N]` emulates exactly this on the
bit-exact model: it plays `N` words of other traffic through the same kernel state
and streams, discards the outputs, then epoch 2 + pre-roll + the vectors. A sweep of
145 cut points across several packets (73 of them leave `ofdm_fe` mid-capture) gives
CSI records, csi_feat windows and feature records **byte-identical** to a clean run.

## 8. Resources / timing (Vitis HLS 2025.2, xcvr1602-2MP, 3.2 ns target)

| Kernel | Est. clock | II / latency | BRAM | DSP | FF | LUT |
|---|---|---|---|---|---|---|
| iq_mux | 1.85 ns | 1 | 0 | 0 | 219 | 292 |
| wifi_ddc | 2.28 ns | 1 (per 80 MSPS input), latency 13 | 1 | 60 | 4.1 k | 4.7 k |
| ofdm_fe | 2.98 ns | 1 (per 20 MSPS sample), latency 14 | 0 | 24 | 10.8 k | 8.6 k |
| ofdm_be | 2.33 ns | 441–2 378 cycles / packet | 28 | 38 | 10.8 k | 24.7 k |
| csi_feat | 2.58 ns | 155 cycles / record, ≤ 1.3 k with window output (time bins: ≤ 64 empty bins filled per record) | 12 | 31 | 10.9 k | 15.0 k |
| feat_pack | 2.06 ns | 126 cycles / window | 0 | 0 | 146 | 398 |
| mm2s / s2mm | 2.19 / 2.01 ns | 1 word / cycle | 2 / 1 | 0 | 1.6 k / 2.2 k | 1.7 k / 2.3 k |

(The xcvr1602 has 550 k LUT, 1.1 M FF, 2 256 DSP, 1 109 BRAM.) Post-route numbers of
the integrated designs are in PROJECT_STATE.md §6.3.

**Bit-exactness rules** that keep C-sim = RTL = golden model: IEEE basic operations,
`sqrtf`, tables and the `fmath.h` polynomials only; `config_op facc/fmacc -impl none`
in `run_hls.tcl` (the inferred DSPFP32 accumulators round a running sum differently
from the sequential C loop — csi_feat windows were a few ULP off in co-simulation);
the DSPFP32 `fmadd`/`fmsub` primitives are not fused (bit-exact, verified).
