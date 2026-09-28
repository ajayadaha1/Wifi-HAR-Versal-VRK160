#!/usr/bin/env bash
# package.sh - v++ --package the linked XSA (lean: no rootfs/SD image).
#
# vrk160_base uses Versal SEGMENTED CONFIGURATION: the linked XSA carries a boot PDI
# (PMC/LPD/FPD/NoC/DDR -> BOOT.BIN, built by PetaLinux) and a PLD PDI (the PL). v++
# therefore takes the linked XSA as the *platform* here and produces the run-time
# half of the design:
#   wifi_har_vrk160.xclbin          XRT metadata (kernels, AIE graph) for host_wifi
#   package/wifi_har_vrk160.pdi     PLD PDI + aie_image partition (graph CDO)
#   package/wifi_har_vrk160.dtbo    DT overlay: fpga-region firmware-name =
#                                   wifi_har_vrk160.pdi + PL nodes + zocl node
# On the board: fpgautil -b wifi_har_vrk160.pdi -o wifi_har_vrk160.dtbo loads PL + AIE
# (wifi-har-autorun.sh), then host_wifi loads the xclbin.
set -o pipefail
cd "$(dirname "$0")"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
XSA="${1:-wifi_har_vrk160.xsa}"
# shellcheck disable=SC1090
source "$VITIS" >/dev/null 2>&1
set -eu
for f in "$XSA" libadf.a; do [ -e "$f" ] || { echo "MISSING: $f (run ./link.sh)"; exit 1; }; done
rm -rf package _x_pkg
v++ -p -t hw --platform "$XSA" --temp_dir _x_pkg \
    libadf.a --package.out_dir package -o wifi_har_vrk160.xclbin
ls -l wifi_har_vrk160.xclbin package/wifi_har_vrk160.pdi package/wifi_har_vrk160.dtbo package/aie.merged.cdo.bin
echo "PACKAGE_DONE"
