#!/usr/bin/env bash
# run_x86sim.sh - functional simulation of the feature graph (x86sim) on the VRK160
# platform, fed with the first csi_feat windows of the golden chain (data/*_input.txt,
# written by build_vivado.sh / copied from golden_chain aie_*_in.txt) and compared with
# the pure-C cores' outputs (data/*golden.txt).
set -o pipefail
cd "$(dirname "$0")"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
: "${PLATFORM:=/proj/gsd/vivado/2025.2/Vitis/base_platforms/vrk160_base/vrk160_base.xpfm}"
: "${PY:=python3}"
# shellcheck disable=SC1090
source "$VITIS" >/dev/null 2>&1
set -eu
rm -rf Work_x86 x86simulator_output
# own archive name: never overwrite the hw libadf.a that v++ links (link.sh)
aiecompiler --target=x86sim --platform="$PLATFORM" --include=./src ./src/graph.cpp --workdir=./Work_x86 \
    --output-archive=libadf_x86.a > x86sim_compile.log 2>&1
x86simulator --pkg-dir=./Work_x86 > x86sim_run.log 2>&1
rc=0
for pair in "output.txt golden.txt" "breath_output.txt breath_golden.txt" "phase_output.txt phase_golden.txt"; do
    set -- $pair
    out=$(find x86simulator_output -name "$1" | head -1)
    echo "== $1 vs $2"
    "$PY" compare.py "$out" "data/$2" 1e-5 || rc=1
done
echo "X86SIM $([ $rc = 0 ] && echo PASS || echo FAIL)"
exit $rc
