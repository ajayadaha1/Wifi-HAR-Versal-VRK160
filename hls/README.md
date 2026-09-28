# PL kernels (Vitis HLS 2025.2)

| Kernel | Source | Control | Role |
|---|---|---|---|
| `iq_mux` | `wifi_ddc/iq_mux.cpp` | ap_ctrl_none, Vivado IP only | RF-DC vs DDR-playback source select, RF-domain reset, ADC sample counter |
| `wifi_ddc` | `wifi_ddc/wifi_ddc.cpp` | ap_ctrl_none, II = 1 | PL NCO (channel select) + 64-tap FIR, 80 → 20 MSPS |
| `ofdm_fe` | `ofdm_rx/ofdm_fe.cpp` | ap_ctrl_none, II = 1 | L-STF detector, L-LTF segmented sign correlator, packet capture |
| `ofdm_be` | `ofdm_rx/ofdm_be.cpp` | ap_ctrl_none | CFO, 3 × FFT-64, CSI, SNR, L-SIG (Viterbi), CSI records; drops `CAP_STALE` captures |
| `csi_feat` | `csi_feat/csi_feat.cpp` | ap_ctrl_none | CSI → amplitude / sanitised phase → AIE windows + tags |
| `feat_pack` | `csi_feat/feat_pack.cpp` | ap_ctrl_none | tag + 3 AIE outputs → feature record |
| `mm2s`, `s2mm` | `movers/*.cpp` | ap_ctrl_chain (XRT) | DDR playback / record drains (`s2mm`: `idle_max` exit + word count at `mem[size]`) |

Shared: `common/wifi_common.h` (formats, address map), `common/fmath.h`
(deterministic float math), generated `common/wifi_tables.h` + `common/ddc_taps.h`
(`python3 -c "import sys; sys.path.insert(0,'csi_ref'); import wifi_ofdm as W;
W.write_hls_tables('hls/common/wifi_tables.h'); W.write_ddc_header('hls/common/ddc_taps.h')"`).

## Flows
```bash
make -C tb check                                   # bit-exact golden chain + Python cross-check
./build_kernels.sh                                 # csim + csynth + export (.xo + hw/ip_repo)
FLOW=csynth,cosim ./build_kernels.sh ofdm_be       # C/RTL co-simulation of one kernel
```
`tb/golden_chain.cpp` compiles the kernel sources with g++ and chains them with
the AIE pure-C cores; it writes every inter-kernel stream to `<outdir>/*.bin`,
which `tb/tb_kernel.cpp` (-DK_<KERNEL>) uses as stimulus / expected output for
C-sim and co-sim. Co-sim builds define `HLS_COSIM_HS` (the per-record free-running
kernels get `ap_ctrl_hs`; the datapath RTL is unchanged) and are never exported.
`run_hls.tcl` disables the inferred DSPFP32 accumulators (`config_op facc|fmacc -impl
none`) — they are not bit-exact with a sequential C loop.

Restart test of the whole chain (docs/OFDM_RX.md §7.1):
```bash
tb/golden_chain <vec>/iq80.bin <out> $(cat <vec>/golden_args.txt) --prefix <other>/iq80.bin [--prefix-cut N]
cmp <vec>/csi_rec.bin <out>/csi_rec.bin && cmp <vec>/feat.bin <out>/feat.bin   # must be identical
```
