#!/usr/bin/env bash
# link.sh - v++ co-generation link of the full VRK160 WiFi-HAR system:
#   AIE graph (libadf.a) + PL kernels (hls/build/xo/*.xo) + RF front end (overlay)
#   -> wifi_har_vrk160.xsa  (fixed platform XSA: PetaLinux input + v++ package input)
#
#   RF_VALIDATE_ONLY=1 ./link.sh   fast structural check (stops after the overlay's
#                                  validate_bd_design, ~3 min)
#   PLATFORM=<xpfm>                 override the platform
set -o pipefail
cd "$(dirname "$0")"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
: "${PLATFORM:=/proj/gsd/vivado/2025.2/Vitis/base_platforms/vrk160_base/vrk160_base.xpfm}"
: "${XO:=../hls/build/xo}"
OUT="${1:-wifi_har_vrk160.xsa}"; TMP="${2:-_x_link}"
# shellcheck disable=SC1090
source "$VITIS" >/dev/null 2>&1
set -u
for f in libadf.a "$XO"/{mm2s,s2mm,wifi_ddc,ofdm_fe,ofdm_be,csi_feat,feat_pack}.xo ../hw/ip_repo/iq_mux/component.xml; do
    [ -e "$f" ] || { echo "MISSING: $f (run ./build_hw.sh and ../hls/build_kernels.sh)"; exit 1; }
done
# The xcvr1602 is ES1: Vivado (vpl) must have beta devices enabled.
grep -qs "enable_beta_device" "$HOME/.Xilinx/Vivado/Vivado_init.tcl" || \
    echo "WARNING: add 'enable_beta_device *' to ~/.Xilinx/Vivado/Vivado_init.tcl (xcvr1602 is an ES1 part)"

# the overlay writes the final integrated BD here (reference capture, see docs)
export WIFI_HAR_BD_CAPTURE="${WIFI_HAR_BD_CAPTURE-$(cd .. && pwd)/hw/scripts/vrk160_wifi_har_bd.tcl}"
echo "LINK_START $(date) out=$OUT validate_only=${RF_VALIDATE_ONLY:-0}"
rm -rf "$TMP"
v++ -l -t hw --platform "$PLATFORM" --temp_dir "$TMP" --config system.cfg \
    --advanced.param compiler.userPreSysLinkOverlayTcl="$PWD/overlay_rf_pre.tcl" \
    --advanced.param compiler.userPostSysLinkOverlayTcl="$PWD/overlay_rf.tcl" \
    --vivado.prop run.impl_1.STEPS.OPT_DESIGN.TCL.PRE="$(cd .. && pwd)/hw/scripts/fix_pvt_sas.tcl" \
    libadf.a "$XO"/{mm2s,s2mm,wifi_ddc,ofdm_fe,ofdm_be,csi_feat,feat_pack}.xo \
    -o "$OUT"
rc=$?
echo "LINK_DONE rc=$rc $(date)"
ls -la "$OUT" 2>/dev/null || true
exit $rc
