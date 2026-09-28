#!/bin/sh
# Boot-time: load the PL + AI Engine image (segmented configuration: BOOT.BIN only holds
# the boot PDI), run the golden self-test (DDR playback -> wifi_ddc -> OFDM RX -> csi_feat
# -> AIE -> DDR, compared with the bit-exact model), print a concise result to the
# console (com0), then start the dashboard (demo playback or live RF, wifi-har.conf).
D=/home/root/wifi-har
CON=/dev/console
MODE=demo; CHAN=6; PORT=8080; SELFTEST=1
[ -f $D/wifi-har.conf ] && . $D/wifi-har.conf
cd $D || exit 1
echo 1 4 1 7 > /proc/sys/kernel/printk 2>/dev/null || true     # quiet the aie driver on com0
# PLD PDI + aie_image (graph CDO) through the FPGA manager; the overlay adds the PL,
# AI Engine and zocl nodes. Skipped when already applied (service restart).
if [ ! -d /configfs/device-tree/overlays/full ]; then
    fpgautil -b $D/wifi_har_vrk160.pdi -o $D/wifi_har_vrk160.dtbo > $D/fpgautil.log 2>&1
    echo "wifi-har: PL+AIE image load: $(cat /sys/class/fpga_manager/fpga0/state 2>/dev/null)" > $CON
fi
modprobe zocl 2>/dev/null || true
for i in $(seq 1 30); do [ -e /dev/dri/renderD128 ] && break; sleep 1; done

if [ "$SELFTEST" = "1" ]; then
    timeout 120 ./host_wifi selftest wifi_har_vrk160.xclbin vectors/quick > selftest.log 2>&1
    rc=$?
    {
        echo ""
        echo "##### WIFI-HAR SELFTEST (rc=$rc) #####"
        grep -E "CSI records|AIE features|runs:|SELFTEST" selftest.log
        echo "######################################"
    } > $CON 2>&1
fi

IP=$(ip -4 -o addr show 2>/dev/null | awk '!/ lo /{print $4}' | cut -d/ -f1 | head -1)
case "$MODE" in
    demo) CMD="./host_wifi play wifi_har_vrk160.xclbin vectors/demo --loop" ;;
    live) CMD="./host_wifi live wifi_har_vrk160.xclbin --chan $CHAN" ;;
    *)    echo "wifi-har: MODE=$MODE -> dashboard not started" > $CON; exit 0 ;;
esac
nohup python3 $D/live_dashboard.py --source exec --cmd "$CMD" --port "$PORT" > $D/dashboard.log 2>&1 &
echo "wifi-har: dashboard ($MODE) on http://${IP:-<board-ip>}:$PORT/" > $CON
exit 0
