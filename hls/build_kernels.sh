#!/usr/bin/env bash
# build_kernels.sh - build every PL kernel (in parallel) with Vitis HLS 2025.2.
#
#   hls/build_kernels.sh                  # csim + csynth + export (xo + Vivado IP)
#   FLOW=csim,csynth,cosim hls/build_kernels.sh ofdm_be   # co-sim one kernel
#
# Needs the golden stream dumps for the testbenches (csim/cosim): build/vectors/{quick,demo}
# written by build_vivado.sh step 1 (make_vectors.py + golden_chain), or by hand:
#   python3 csi_ref/make_vectors.py --out build/vectors/quick --preset quick
#   hls/tb/golden_chain build/vectors/quick/iq80.bin build/vectors/quick \
#       $(cat build/vectors/quick/golden_args.txt)          (same for --preset demo)
# Override with TBDIR=<quick dir> TBDIR_DEMO=<demo dir>.
# Outputs: hls/build/xo/<k>.xo (v++), hw/ip_repo/<k>/ (Vivado), hls/build/<k>/ (logs).
set -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
: "${FLOW:=csim,csynth,export}"
: "${TBDIR:=$HERE/../build/vectors/quick}"
: "${TBDIR_DEMO:=$HERE/../build/vectors/demo}"
TBDIR="$(readlink -f "$TBDIR")"; TBDIR_DEMO="$(readlink -f "$TBDIR_DEMO")"
export FLOW TBDIR TBDIR_DEMO
if echo "$FLOW" | grep -q "csim\|cosim"; then
    for f in "$TBDIR/s20.bin" "$TBDIR_DEMO/csi.bin"; do
        [ -s "$f" ] || { echo "ERROR: no golden stream dumps ($f) - run build_vivado.sh step 1 or see the header"; exit 1; }
    done
fi
# shellcheck disable=SC1090
source "$VITIS" >/dev/null 2>&1   # (before set -u: settings64.sh reads unset vars)
set -u
command -v vitis-run >/dev/null || { echo "ERROR: vitis-run not on PATH ($VITIS)"; exit 1; }

#        kernel     source                 testbench define   extra tb sources
TABLE="wifi_ddc  wifi_ddc/wifi_ddc.cpp  K_WIFI_DDC  -
iq_mux    wifi_ddc/iq_mux.cpp    K_IQ_MUX    -
ofdm_fe   ofdm_rx/ofdm_fe.cpp    K_OFDM_FE   -
ofdm_be   ofdm_rx/ofdm_be.cpp    K_OFDM_BE   -
csi_feat  csi_feat/csi_feat.cpp  K_CSI_FEAT  -
feat_pack csi_feat/feat_pack.cpp K_FEAT_PACK -
mm2s      movers/mm2s.cpp        K_MOVERS    movers/s2mm.cpp
s2mm      movers/s2mm.cpp        K_MOVERS    movers/mm2s.cpp"

SEL="${*:-}"
mkdir -p "$HERE/build"
pids=()
while read -r k src tb extra; do
    [ -n "$SEL" ] && ! echo " $SEL " | grep -q " $k " && continue
    d="$HERE/build/$k"; rm -rf "$d"; mkdir -p "$d"
    (
        cd "$d" || exit 1
        if [ "$extra" = "-" ]; then unset EXTRA_TB; else export EXTRA_TB="$extra"; fi
        if [ "$k" = iq_mux ]; then export FLOWTGT=vivado; else export FLOWTGT=vitis; fi
        K="$k" SRC="$src" TBDEF="$tb" vitis-run --mode hls --tcl "$HERE/run_hls.tcl" > hls.log 2>&1
        echo "$k rc=$?" > done.txt
    ) &
    pids+=($!)
done <<< "$TABLE"
for p in "${pids[@]}"; do wait "$p"; done

echo "=== HLS summary (FLOW=$FLOW) ==="
for d in "$HERE"/build/*/; do
    k=$(basename "$d"); [ "$k" = xo ] && continue
    [ -f "$d/done.txt" ] || continue
    log="$d/hls.log"
    tb=$(grep -ho "TB_[A-Z_]* \(PASS\|FAIL\)" "$log" | sort -u | tr '\n' ' ')
    co=$(grep -o "C/RTL co-simulation finished: [A-Z]*" "$log" | tail -1)
    rpt=$(ls "$d"/${k}_prj/sol1/syn/report/${k}_csynth.rpt 2>/dev/null | head -1)
    est=""; [ -n "$rpt" ] && est=$(grep -m1 "^ *|ap_clk *|" "$rpt" | awk -F'|' '{gsub(/ /,"",$4); print "est="$4}')
    err=$(grep -c "^ERROR" "$log")
    printf "%-10s %-14s %-40s %-12s errors=%s\n" "$k" "$(cat "$d/done.txt" | cut -d' ' -f2)" "$tb $co" "$est" "$err"
done
