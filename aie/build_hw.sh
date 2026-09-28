#!/usr/bin/env bash
# build_hw.sh - compile the 3-branch AIE feature graph for the VRK160 (VR1602,
# AIE1 42x3) against the vrk160_base Vitis platform -> ./libadf.a
#
#   PLATFORM=<xpfm>  override the platform (default: Vitis 2025.2 vrk160_base)
#   TARGET=x86sim    functional simulation build -> libadf_x86.a (then: x86simulator --pkg-dir=Work_x86)
set -o pipefail
cd "$(dirname "$0")"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
: "${PLATFORM:=/proj/gsd/vivado/2025.2/Vitis/base_platforms/vrk160_base/vrk160_base.xpfm}"
: "${TARGET:=hw}"
# shellcheck disable=SC1090
source "$VITIS" >/dev/null 2>&1   # (not under set -u: settings64.sh reads unset vars)
set -u
command -v aiecompiler >/dev/null || { echo "ERROR: aiecompiler not on PATH ($VITIS)"; exit 1; }

WORK=Work_hw; LIB=libadf.a
[ "$TARGET" = x86sim ] && { WORK=Work_x86; LIB=libadf_x86.a; }   # never clobber the hw archive
rm -rf "$WORK"
aiecompiler --target="$TARGET" --platform="$PLATFORM" --include=./src \
    ./src/graph.cpp --workdir="./$WORK" --output-archive="$LIB"
rc=$?
echo "AIE_${TARGET}_DONE rc=$rc $LIB bytes=$(stat -c%s "$LIB" 2>/dev/null || echo 0)"
exit $rc
