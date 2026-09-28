SUMMARY = "VRK160 WiFi-CSI HAR: XRT host, xclbin, live dashboard, self-test vectors, boot service"
DESCRIPTION = "Prebuilt aarch64 XRT host (host_wifi), the co-generated wifi_har_vrk160.xclbin, \
the run-time PL + AIE image (wifi_har_vrk160.pdi + .dtbo, loaded with fpgautil), \
the pure-Python dashboard (live_dashboard.py + features8.py), ofdm_ctl.py (control tool of \
the plain-Vivado OFDM-receiver design), golden playback vectors \
(vectors/quick = OFDM RX self-test, vectors/demo = activity scenario) under \
/home/root/wifi-har, and a systemd service that runs the self-test at boot, prints \
PASS/FAIL to the console and starts the dashboard (demo playback or live RF, see \
wifi-har.conf). Files are staged into files/ by build_petalinux.sh."
LICENSE = "CLOSED"

SRC_URI = "file://host_wifi \
           file://wifi_har_vrk160.xclbin \
           file://wifi_har_vrk160.pdi \
           file://wifi_har_vrk160.dtbo \
           file://live_dashboard.py \
           file://features8.py \
           file://ofdm_ctl.py \
           file://vectors \
           file://wifi-har.conf \
           file://wifi-har-autorun.sh \
           file://wifi-har.service"

S = "${WORKDIR}"

# Installs prebuilt artifacts only (the host is cross-built against the SDK by
# build_petalinux.sh): no cross toolchain, no strip / debug split of the ELF.
INHIBIT_DEFAULT_DEPS = "1"
INSANE_SKIP:${PN} += "arch already-stripped ldflags file-rdeps"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
RDEPENDS:${PN} += "python3-core python3-json python3-netserver python3-threading fpga-manager-script"

FILES:${PN} += "/home/root/wifi-har /etc/systemd/system"

do_install() {
    install -d ${D}/home/root/wifi-har
    install -m 0755 ${S}/host_wifi ${D}/home/root/wifi-har/host_wifi
    install -m 0644 ${S}/wifi_har_vrk160.xclbin ${D}/home/root/wifi-har/wifi_har_vrk160.xclbin
    install -m 0644 ${S}/wifi_har_vrk160.pdi ${D}/home/root/wifi-har/wifi_har_vrk160.pdi
    install -m 0644 ${S}/wifi_har_vrk160.dtbo ${D}/home/root/wifi-har/wifi_har_vrk160.dtbo
    install -m 0755 ${S}/live_dashboard.py ${D}/home/root/wifi-har/live_dashboard.py
    install -m 0644 ${S}/features8.py ${D}/home/root/wifi-har/features8.py
    install -m 0755 ${S}/ofdm_ctl.py ${D}/home/root/wifi-har/ofdm_ctl.py
    install -m 0644 ${S}/wifi-har.conf ${D}/home/root/wifi-har/wifi-har.conf
    install -m 0755 ${S}/wifi-har-autorun.sh ${D}/home/root/wifi-har/wifi-har-autorun.sh
    for d in quick demo; do
        install -d ${D}/home/root/wifi-har/vectors/$d
        for f in ${S}/vectors/$d/*; do
            install -m 0644 $f ${D}/home/root/wifi-har/vectors/$d/
        done
    done
    # systemd unit + manual enable symlink (no systemctl-native needed)
    install -d ${D}/etc/systemd/system/multi-user.target.wants
    install -m 0644 ${S}/wifi-har.service ${D}/etc/systemd/system/wifi-har.service
    ln -sf ../wifi-har.service ${D}/etc/systemd/system/multi-user.target.wants/wifi-har.service
}
