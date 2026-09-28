## boot_jtag.tcl - JTAG boot of the VRK160 WiFi-HAR image (board farm / lab, no SD card).
## Mirrors the VCK190 boot_d1.tcl: program BOOT.BIN (boot PDI + PLM/PSM + dtb + BL31 +
## U-Boot), freeze U-Boot, download Image / dtb / initramfs into DDR, release; then run
##   booti 0x00200000 0x04000000 0x00001000
## on the U-Boot console. vrk160_base uses segmented configuration: the wifi-har service
## then loads the PL + AIE image (fpgautil, wifi_har_vrk160.pdi/.dtbo) and prints the
## SELFTEST result to com0.
## Usage: BOARD_URL=TCP:<boardhost>:3121 [IMG=<images/linux>] xsdb petalinux/scripts/boot_jtag.tcl
set IMG [expr {[info exists ::env(IMG)] ? $::env(IMG) : [file normalize [file dirname [info script]]/../images/linux]}]
set URL $::env(BOARD_URL)
foreach f {BOOT.BIN Image system.dtb rootfs.cpio.gz.u-boot} {
    if {![file exists $IMG/$f]} { puts stderr "FATAL: missing $IMG/$f"; exit 1 }
}
proc reconnect {u} { catch {disconnect}; after 2000; connect -url $u; after 4000 }
reconnect $URL
targets -set -nocase -filter {name =~ "PMC"}
puts stderr "INFO: rst -system"; catch {rst -system}; after 8000
catch {targets -set -nocase -filter {name =~ "PMC"}}
puts stderr "INFO: device program BOOT.BIN"
if {[catch {device program $IMG/BOOT.BIN} e]} { puts stderr "FATAL: device program failed: $e"; exit 1 }
puts stderr "INFO: PROGRAM_OK"; after 2000
targets -set -nocase -filter {name =~ "*A72*#0"}
catch {con}; after 3000; catch {stop}
puts stderr "INFO: U-Boot frozen"
puts stderr "INFO: Image -> 0x00200000";      dow -data $IMG/Image 0x00200000
puts stderr "INFO: system.dtb -> 0x00001000"; dow -data $IMG/system.dtb 0x00001000
puts stderr "INFO: initramfs -> 0x04000000 (several minutes)"; dow -data $IMG/rootfs.cpio.gz.u-boot 0x04000000
after 500; catch {con}
puts stderr "INFO: DOWNLOAD_DONE - type on the console: booti 0x00200000 0x04000000 0x00001000"
exit 0
