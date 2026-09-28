#!/usr/bin/env bash
# build_petalinux.sh - ONE-SHOT PetaLinux build for the VRK160 WiFi-CSI HAR design
# (run from the repo root, after ./build_vivado.sh). The PetaLinux project lives in
# ./petalinux (tracked: project-spec/ + scripts/; build outputs are ignored).
#
#   1) import HW   petalinux/scripts/import_hw.sh aie/wifi_har_vrk160.xsa  (XSA -> SDT -> config)
#   2) SDK         petalinux-build --sdk ; sdk.sh -> $SDK_DIR (aarch64 XRT sysroot)
#   3) host        cross-compile aie/host/host_wifi.cpp against the SDK
#   4) stage       host_wifi + xclbin + PL/AIE pdi+dtbo + dashboard + vectors
#                  -> recipes-apps/wifi-har/files
#   5) rootfs      petalinux-build                       (bakes the wifi-har recipe)
#   6) BOOT.BIN    petalinux/scripts/build_boot.sh       (boot PDI + PLM/PSM + dtb + BL31/U-Boot;
#                  segmented configuration: the PL + AIE image is loaded at run time)
#   7) SD files    petalinux/images/linux/sd/{BOOT.BIN,boot.scr,image.ub}
#
# PREREQ (AMD internal env):
#   TMPDIR is forced to /tmp/petalinux-$USER (override: PLNX_TMPDIR, local disk)
#   PetaLinux 2025.2 settings.sh (default path below) ; Vitis 2025.2 for sdtgen/bootgen
# Env overrides: XSA, SDK_DIR, PLNX_SETTINGS, VITIS, VEC (vector dir from build_vivado.sh),
#                SKIP_SDK=1 (reuse an installed SDK in $SDK_DIR: host/app-only rebuilds)
set -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PLNX="$HERE/petalinux"
: "${XSA:=$HERE/aie/wifi_har_vrk160.xsa}"
: "${SDK_DIR:=/tmp/vrk160-sdk-$USER}"
: "${VEC:=$HERE/build/vectors}"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
: "${PLNX_SETTINGS:=/proj/petalinux/2025.2/petalinux-v2025.2_11160223/tool/petalinux-v2025.2-final/settings.sh}"
# local, whitespace-free TMPDIR (PetaLinux host checks compile test programs in it)
export TMPDIR="${PLNX_TMPDIR:-/tmp/petalinux-$USER}"; mkdir -p "$TMPDIR"
export VITIS PLNX_SETTINGS
FILES="$PLNX/project-spec/meta-user/recipes-apps/wifi-har/files"
PKG="$HERE/aie/package"
for f in "$XSA" "$HERE/aie/wifi_har_vrk160.xclbin" "$PKG/wifi_har_vrk160.pdi" "$PKG/wifi_har_vrk160.dtbo" \
         "$VEC/quick/iq80.bin" "$VEC/demo/iq80.bin"; do
    [ -e "$f" ] || { echo "ERROR: missing $f (run ./build_vivado.sh first)"; exit 1; }
done
plnx() { ( cmd=("$@"); set --          # settings.sh reads "$1" as its install dir
           source "$PLNX_SETTINGS" >/dev/null 2>&1
           unset XILINX_VITIS XILINX_VIVADO XILINX_HLS XILINX_XRT LD_LIBRARY_PATH
           cd "$PLNX" && "${cmd[@]}" ); }
set -eu

stage() {                                              # the wifi-har recipe's files/
    cp -f "$HERE/aie/wifi_har_vrk160.xclbin" "$PKG/wifi_har_vrk160.pdi" "$PKG/wifi_har_vrk160.dtbo" "$FILES/"
    cp -f "$HERE/live/live_dashboard.py" "$HERE/csi_ref/features8.py" "$HERE/sw/ofdm_ctl.py" "$FILES/"
    for p in quick demo; do
        rm -rf "$FILES/vectors/$p"; mkdir -p "$FILES/vectors/$p"
        cp -f "$VEC/$p"/{iq80.bin,csi_rec.bin,feat.bin,vectors.json,chunks.txt} "$FILES/vectors/$p/"
    done
}
# every SRC_URI file of the wifi-har recipe must exist whenever bitbake parses recipes
stage
[ -s "$FILES/host_wifi" ] || : > "$FILES/host_wifi"    # placeholder until step 3

echo "== [1/7] import hardware (XSA -> SDT -> petalinux-config) =="
mkdir -p "$PLNX/build"
"$PLNX/scripts/import_hw.sh" "$XSA" "$PLNX/build/sdt"
# device-tree's do_configure appends '#include "system-user.dtsi"' to the DTS in its
# work dir; re-running it after a new import (without a fresh unpack) includes it twice
# -> "Duplicate node name" in system-conf.dtsi. Start that recipe from scratch.
plnx petalinux-build -c device-tree -x cleansstate

echo "== [2/7] SDK (aarch64 sysroot with XRT) =="
if [ "${SKIP_SDK:-0}" = "1" ] && ls "$SDK_DIR"/environment-setup-* >/dev/null 2>&1; then
    echo "SKIP_SDK=1: reusing $SDK_DIR"
else
    plnx petalinux-build --sdk
    rm -rf "$SDK_DIR"
    "$PLNX/images/linux/sdk.sh" -d "$SDK_DIR" -y >/dev/null
fi

echo "== [3/7] cross-compile host_wifi =="
ENVSETUP="$(ls "$SDK_DIR"/environment-setup-* | head -1)"
( unset LD_LIBRARY_PATH XILINX_VITIS XILINX_VIVADO XILINX_HLS XILINX_XRT
  # shellcheck disable=SC1090
  source "$ENVSETUP"
  cd "$HERE/aie/host"
  $CXX -std=c++17 -O2 host_wifi.cpp -o host_wifi -lxrt_coreutil -luuid -pthread   # xrt::uuid is inline libuuid
  $STRIP host_wifi )

echo "== [4/7] stage the wifi-har recipe =="
stage
cp -f "$HERE/aie/host/host_wifi" "$FILES/"

echo "== [5/7] final petalinux-build =="
plnx petalinux-build

echo "== [6/7] BOOT.BIN (boot PDI of the XSA + PLM/PSM + dtb + BL31/U-Boot) =="
"$PLNX/scripts/build_boot.sh" "$XSA" "$PLNX/images/linux"

echo "== [7/7] SD card files =="
SD="$PLNX/images/linux/sd"; mkdir -p "$SD"
cp -f "$PLNX/images/linux/BOOT.BIN" "$PLNX/images/linux/boot.scr" "$PLNX/images/linux/image.ub" "$SD/"
echo
echo "DONE: $SD/{BOOT.BIN,boot.scr,image.ub} -> FAT32 partition of the SD card"
echo "      boot log shows 'wifi-har: PL+AIE image load: operating', '##### WIFI-HAR SELFTEST'"
echo "      and the dashboard URL (port 8080)"
