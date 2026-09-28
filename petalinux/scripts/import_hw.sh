#!/usr/bin/env bash
# import_hw.sh - import a VRK160 XSA into the PetaLinux project (SDT flow):
#   XSA --sdtgen (sdt.tcl)--> SDT dir --petalinux-config --get-hw-description--> project
# Usage (from anywhere): petalinux/scripts/import_hw.sh <design.xsa> [sdt_outdir]
# Needs Vitis (sdtgen) and PetaLinux 2025.2; run in a shell WITHOUT the Vitis env
# for petalinux-* (this script isolates the two).
set -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PLNX="$(cd "$HERE/.." && pwd)"
XSA="$(readlink -f "$1")"; SDT="${2:-$PLNX/build/sdt}"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
: "${PLNX_SETTINGS:=/proj/petalinux/2025.2/petalinux-v2025.2_11160223/tool/petalinux-v2025.2-final/settings.sh}"
[ -f "$XSA" ] || { echo "no XSA: $1"; exit 1; }
( source "$VITIS" >/dev/null 2>&1; sdtgen "$HERE/sdt.tcl" "$XSA" "$SDT" ) > "$PLNX/build/sdtgen.log" 2>&1 \
    || { echo "sdtgen failed (see $PLNX/build/sdtgen.log)"; exit 1; }
ls "$SDT"/*.pdi >/dev/null 2>&1 || { echo "SDT has no PDI - not a fixed (post-impl) XSA?"; exit 1; }
(
    # PetaLinux needs a local, whitespace-free TMPDIR (its host-tool checks compile
    # test programs there and misreport "ncurses missing" otherwise)
    export TMPDIR="${PLNX_TMPDIR:-/tmp/petalinux-$USER}"; mkdir -p "$TMPDIR"
    # the Yocto TMPDIR (project config) follows the user: local disk, per user
    sed -i "s#^CONFIG_TMP_DIR_LOCATION=.*#CONFIG_TMP_DIR_LOCATION=\"$TMPDIR/vrk160-tmp\"#" \
        "$PLNX/project-spec/configs/config"
    # shellcheck disable=SC1090
    set --                                   # settings.sh reads "$1" as its install dir
    source "$PLNX_SETTINGS" >/dev/null 2>&1
    unset XILINX_VITIS XILINX_VIVADO XILINX_HLS XILINX_XRT LD_LIBRARY_PATH
    cd "$PLNX" && petalinux-config --get-hw-description="$SDT" --silentconfig
) || exit 1
# Base PDI for BOOT.BIN: gen-machineconf keeps the PDI name of the FIRST import in an
# existing machine conf (seen: a stale placeholder.pdi -> base-pdi "No base pdi
# supplied"). Pin it to what this XSA provides: *_boot.pdi for a segmented
# configuration (v++ designs on vrk160_base), else the single .pdi (plain design).
PDI="$(ls "$SDT"/*_boot.pdi 2>/dev/null | head -1)"; [ -n "$PDI" ] || PDI="$(ls "$SDT"/*.pdi | head -1)"
for mc in "$PLNX"/build/conf/machine/*.conf; do
    sed -i -E 's#^PDI_PATH = "\$\{PDI_PATH_DIR\}/[^"]*"#PDI_PATH = "${PDI_PATH_DIR}/'"$(basename "$PDI")"'"#' "$mc"
    grep -H "^PDI_PATH =" "$mc"
done
echo "IMPORT_HW_DONE $XSA -> $SDT"
