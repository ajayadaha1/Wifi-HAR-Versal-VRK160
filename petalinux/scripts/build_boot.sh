#!/usr/bin/env bash
# build_boot.sh - BOOT.BIN for the VRK160 WiFi-HAR design from an XSA + PetaLinux images.
#
# vrk160_base (and hence the v++-linked XSA) uses Versal SEGMENTED CONFIGURATION:
#   *_boot.pdi  PMC/LPD/FPD, NoC + DDR, AIE array init   -> BOOT.BIN (this script)
#   *_pld.pdi   the PL                                   -> run time: v++ package
#               (aie/package/wifi_har_vrk160.pdi = PLD + aie_image graph CDO, loaded
#               with fpgautil + wifi_har_vrk160.dtbo by wifi-har-autorun.sh)
# A non-segmented XSA (the plain-Vivado design) has one full PDI, which goes here as is.
#
# BIF: boot PDI (+ PetaLinux PLM / PSM firmware) + system.dtb @0x1000 + BL31 + U-Boot
# (the same content petalinux-build's xilinx-bootbin produces; rebuilding it here pins
# the PDI to the given XSA and needs no petalinux-build when only the XSA changed).
# Usage: build_boot.sh <xsa> <petalinux images/linux dir> [out]
set -o pipefail
XSA="$1"; IMG="$2"; OUT="${3:-$IMG/BOOT.BIN}"
: "${VITIS:=/proj/gsd/vivado/2025.2/Vitis/settings64.sh}"
# shellcheck disable=SC1090
source "$VITIS" >/dev/null 2>&1
set -eu
command -v bootgen >/dev/null || { echo "no bootgen on PATH"; exit 1; }
for f in "$XSA" "$IMG/plm.elf" "$IMG/psmfw.elf" "$IMG/bl31.elf" "$IMG/u-boot.elf" "$IMG/system.dtb"; do
    [ -f "$f" ] || { echo "missing: $f"; exit 1; }
done
WORK="$(dirname "$OUT")/boot_work"
rm -rf "$WORK"; mkdir -p "$WORK"
PDI=$(unzip -Z1 "$XSA" | grep -E '_boot\.pdi$' | head -1)
[ -n "$PDI" ] || PDI=$(unzip -Z1 "$XSA" | grep -E '\.pdi$' | grep -v '_pld\.pdi$' | head -1)
[ -n "$PDI" ] || { echo "no boot/full .pdi inside $XSA"; exit 1; }
unzip -o -q -j "$XSA" "$PDI" -d "$WORK"
cp -f "$WORK/$(basename "$PDI")" "$WORK/base-design.pdi"
cat > "$WORK/boot.bif" <<BIF
the_ROM_image:
{
image {
        { type=bootimage, file=$WORK/base-design.pdi }
        { type=bootloader, file=$IMG/plm.elf }
        { core=psm, file=$IMG/psmfw.elf }
}
image {
        id = 0x1c000000, name=apu_subsystem
        { type=raw, load=0x1000, file=$IMG/system.dtb }
        { core=a72-0, exception_level=el-3, trustzone, file=$IMG/bl31.elf }
        { core=a72-0, exception_level=el-2, file=$IMG/u-boot.elf }
}
}
BIF
bootgen -arch versal -image "$WORK/boot.bif" -w -o "$OUT" > "$WORK/bootgen.log" 2>&1 || { cat "$WORK/bootgen.log"; exit 1; }
ls -l "$OUT"
echo "BOOT_BIN_DONE $OUT (pdi=$(basename "$PDI"))"
