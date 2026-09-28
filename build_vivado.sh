#!/usr/bin/env bash
# build_vivado.sh - ONE-SHOT hardware build of the VRK160 WiFi-CSI HAR design
# (run from the repo root). Everything from source:
#
#   1) golden   csi_ref/make_vectors.py + hls/tb/golden_chain
#                                        -> build/vectors/{quick,demo}  (self-test + demo
#                                           vectors, and the HLS testbench stream dumps)
#   2) HLS      hls/build_kernels.sh     8 PL kernels -> hls/build/xo/*.xo (v++)
#                                                     + hw/ip_repo/*    (Vivado IP)
#   3) AIE      aie/build_hw.sh          feature graph -> aie/libadf.a  (vrk160_base, AIE1)
#   4) link     aie/link.sh              v++ -l + overlay_rf.tcl (RF front end)
#                                        -> aie/wifi_har_vrk160.xsa       <- PetaLinux input
#   5) package  aie/package.sh           -> aie/wifi_har_vrk160.xclbin + aie/package/wifi_har_vrk160.{pdi,dtbo}
#                                           (segmented configuration: run-time PL + AIE image)
#   6) (PLAIN=1) hw/scripts/project_top.tcl  plain-Vivado OFDM-receiver design (no AIE)
#                                        -> hw/build/vrk160_ofdm_rx.xsa
# Then: ./build_petalinux.sh
#
# Env: VITIS (settings64.sh), PLATFORM (.xpfm), PY (python with numpy+scipy), PLAIN=1.
# Needs 'enable_beta_device *' in ~/.Xilinx/Vivado/Vivado_init.tcl (xcvr1602 is ES1).
set -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
: "${PY:=python3}"
export VITIS
"$PY" -c "import numpy, scipy" 2>/dev/null || { echo "ERROR: $PY needs numpy + scipy (pip install numpy scipy)"; exit 1; }
set -eu
VEC="$HERE/build/vectors"

echo "== [1/6] golden vectors + bit-exact model =="
make -C "$HERE/hls/tb" golden_chain PY="$PY" >/dev/null
for p in quick demo; do
    mkdir -p "$VEC/$p"
    "$PY" "$HERE/csi_ref/make_vectors.py" --out "$VEC/$p" --preset "$p"
    # shellcheck disable=SC2046
    "$HERE/hls/tb/golden_chain" "$VEC/$p/iq80.bin" "$VEC/$p" $(cat "$VEC/$p/golden_args.txt")
    "$PY" "$HERE/csi_ref/check_golden.py" "$VEC/$p" | tail -1
done

echo "== [2/6] HLS kernels (csim + csynth + export) =="
TBDIR="$VEC/quick" TBDIR_DEMO="$VEC/demo" "$HERE/hls/build_kernels.sh" | tee "$HERE/build/hls_summary.txt"
grep -q "FAIL\|rc=[1-9]" "$HERE/build/hls_summary.txt" && { echo "ERROR: HLS build failed"; exit 1; }
for k in mm2s s2mm wifi_ddc ofdm_fe ofdm_be csi_feat feat_pack; do [ -s "$HERE/hls/build/xo/$k.xo" ]; done

echo "== [3/6] AIE feature graph (x86sim vs golden, then hw) =="
cp -f "$VEC/demo/aie_mot_in.txt" "$HERE/aie/data/input.txt"
cp -f "$VEC/demo/aie_brt_in.txt" "$HERE/aie/data/breath_input.txt"
cp -f "$VEC/demo/aie_phs_in.txt" "$HERE/aie/data/phase_input.txt"
"$PY" - "$VEC/demo" "$HERE/aie/data" <<'PYEOF'
import sys, numpy as np
src, dst = sys.argv[1], sys.argv[2]
for name, n, out in (("aie_mot_out.bin", 3, "golden.txt"), ("aie_brt_out.bin", 33, "breath_golden.txt"),
                     ("aie_phs_out.bin", 3, "phase_golden.txt")):
    np.savetxt(f"{dst}/{out}", np.fromfile(f"{src}/{name}", dtype=np.float32)[:n], fmt="%.9e")
PYEOF
PY="$PY" "$HERE/aie/run_x86sim.sh" | tail -1
"$HERE/aie/build_hw.sh"

echo "== [4/6] v++ link (co-generation + RF overlay) =="
"$HERE/aie/link.sh" wifi_har_vrk160.xsa _x_link
[ -s "$HERE/aie/wifi_har_vrk160.xsa" ] || { echo "ERROR: link produced no XSA"; exit 1; }

echo "== [5/6] v++ package =="
"$HERE/aie/package.sh" wifi_har_vrk160.xsa

if [ "${PLAIN:-0}" = "1" ]; then
    echo "== [6/6] plain-Vivado OFDM receiver design =="
    ( source "$VITIS" >/dev/null 2>&1; cd "$HERE" && vivado -mode batch -nojournal \
        -log "$HERE/hw/build/project_top.log" -source "$HERE/hw/scripts/project_top.tcl" )
else
    echo "== [6/6] plain-Vivado design skipped (PLAIN=1 to build hw/build/vrk160_ofdm_rx.xsa) =="
fi
echo
echo "DONE:"
echo "  XSA    : $HERE/aie/wifi_har_vrk160.xsa        -> ./build_petalinux.sh"
echo "  xclbin : $HERE/aie/wifi_har_vrk160.xclbin"
echo "  PL+AIE : $HERE/aie/package/wifi_har_vrk160.{pdi,dtbo}   (fpgautil on the board)"
echo "  vectors: $VEC/{quick,demo}"
