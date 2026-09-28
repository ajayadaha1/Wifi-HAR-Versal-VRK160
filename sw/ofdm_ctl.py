#!/usr/bin/env python3
"""ofdm_ctl.py - control tool for the PLAIN-Vivado VRK160 OFDM-receiver design
(hw/scripts/project_top.tcl -> hw/build/vrk160_ofdm_rx.xsa; no XRT, no AIE).

  ofdm_ctl.py status
  ofdm_ctl.py selftest <vecdir>        DDR playback -> OFDM RX -> CSI records, compare
                                       with <vecdir>/csi_rec.bin (golden_chain; identical
                                       to its csi stream: every packet is recorded)
  ofdm_ctl.py play <vecdir> [--loop]   paced playback, prints CSI/STAT lines
  ofdm_ctl.py live [--chan N]          RF-ADC tile 200 -> CSI/STAT lines

The lines are the host_wifi format, so the dashboard works unchanged:
  python3 live_dashboard.py --source exec --cmd "python3 ofdm_ctl.py live --chan 6"
(the plain design has no AIE: the feature panels stay empty, the OFDM-receiver /
CSI panels are live).

DMA buffers live in a no-map reserved-memory carve-out declared in the device tree
(petalinux/scripts/plain_design.dtsi): RESV_PA 0x6000_0000, 256 MB, inside the
DDR_CH0_LEGACY aperture the PL masters are mapped to (vrk160_ofdm_design.tcl).
Pure Python 3 standard library (mmap on /dev/mem).
"""
import argparse
import json
import mmap
import os
import struct
import sys
import time

PL_BASE, PL_SPAN = 0xA5000000, 0x00400000
IQ_MUX, WIFI_DDC, OFDM_FE, OFDM_BE, MM2S, S2MM = 0x0, 0x10000, 0x20000, 0x30000, 0x50000, 0x60000
RESV_PA, RESV_SPAN = 0x60000000, 0x10000000
IQ_OFF, CSI_OFF = 0x0, 0x0C000000                 # 192 MB playback area, 64 MB CSI ring
REC_WORDS = 144
REC_MAGIC = 0xC51F0001
AP_START, AP_DONE, AP_IDLE, AP_CONTINUE = 1, 2, 4, 0x10    # movers are ap_ctrl_chain
PL_CLK_HZ = 312.5e6                               # kernel clock: s2mm idle_max unit


class Mem:
    def __init__(self, base, span):
        self.fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
        self.m = mmap.mmap(self.fd, span, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=base)

    def w(self, off, v):
        self.m[off:off + 4] = struct.pack("<I", v & 0xFFFFFFFF)

    def r(self, off):
        return struct.unpack("<I", self.m[off:off + 4])[0]

    def wf(self, off, f):
        self.m[off:off + 4] = struct.pack("<f", f)


def f32(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def configure(pl, nco, playback):
    pl.w(IQ_MUX + 0x10, 0x3 if playback else 0x0)
    if not playback:
        time.sleep(0.1)
        pl.w(IQ_MUX + 0x10, 0x4)                  # release the RF fabric MMCM reset
    pl.w(WIFI_DDC + 0x10, nco)
    pl.w(WIFI_DDC + 0x18, 0)
    pl.wf(OFDM_FE + 0x10, 0.49)
    pl.wf(OFDM_FE + 0x18, 0.0)
    pl.w(OFDM_FE + 0x20, 24)
    pl.w(OFDM_FE + 0x28, 480)
    pl.w(OFDM_BE + 0x10, 0)                       # rec stream unconnected in this design
    e = (pl.r(OFDM_FE + 0x30) + 1) & 0xFFFFFFFF
    for base, off in ((WIFI_DDC, 0x20), (OFDM_FE, 0x30), (OFDM_BE, 0x18)):
        pl.w(base + off, e)


def start(pl, base, pa, words, idle_s=0.0):
    """mm2s(mem, size) / s2mm(mem, size, idle_max): s2mm also returns after idle_s
    seconds without data (0 = blocking) and writes the word count to mem[size]"""
    pl.w(base + 0x10, pa & 0xFFFFFFFF)
    pl.w(base + 0x14, pa >> 32)
    pl.w(base + 0x1C, words)
    if base == S2MM:
        pl.w(base + 0x24, int(min(idle_s * PL_CLK_HZ, 2e9)))
    pl.w(base + 0x00, AP_START)


def wait(pl, base, timeout):
    t = time.time() + timeout
    while time.time() < t:
        if pl.r(base) & (AP_DONE | AP_IDLE):
            pl.w(base, AP_CONTINUE)               # ap_ctrl_chain: acknowledge ap_done
            return True
        time.sleep(0.001)
    return False


def dbfs(p):
    import math
    return 10.0 * math.log10(max(p, 1e-3) / (32768.0 * 32768.0))


def csi_line(w):
    import math
    t = ((w[3] << 32) | w[2]) / 20e6
    s = "CSI,%u,%.6f,%u,%u,%u,%.0f,%.1f,%.1f" % (w[1], t, (w[4] >> 24) & 1, (w[12] >> 16) & 0xFF, w[13],
                                               f32(w[5]), dbfs(f32(w[6])), 10 * math.log10(max(1e-6, f32(w[7]))))
    for kk in range(-32, 32):
        k = kk & 63
        s += ",%.4g" % math.hypot(f32(w[16 + 2 * k]), f32(w[17 + 2 * k]))
    return s


def stat_line(pl):
    return "STAT,%u,%u,%u,%u,%u,0,0,%.1f" % (pl.r(IQ_MUX + 0x18), pl.r(OFDM_FE + 0x38), pl.r(OFDM_BE + 0x20),
                                            pl.r(OFDM_BE + 0x30), pl.r(OFDM_BE + 0x50),
                                            dbfs(f32(pl.r(OFDM_FE + 0x48)) / 48.0))


# Restart protocol (hls/common/wifi_common.h): every epoch bump only ARMS the kernels;
# PREROLL zero words let a capture still in flight complete (flagged stale, dropped)
# before the new data, so every run is bit-identical to a fresh one.
PREROLL, FLUSH = 2048, 4096


def load_iq(vd):
    with open(os.path.join(vd, "iq80.bin"), "rb") as f:
        return b"\0" * (PREROLL * 4) + f.read() + b"\0" * (FLUSH * 4)      # pre-roll + flush


def s2mm_count(dm, off, words):
    return min(struct.unpack("<I", dm.m[off + words * 4:off + words * 4 + 4])[0], words)


def purge(pl, dm):
    """drop CSI records a previous (killed) session left in the stream FIFOs"""
    tot = 0
    for _ in range(64):
        start(pl, S2MM, RESV_PA + CSI_OFF, 16384, idle_s=0.05)
        wait(pl, S2MM, 5)
        n = s2mm_count(dm, CSI_OFF, 16384)
        tot += n
        if n < 16384:
            break
    if tot:
        print("purge: dropped %d stale words of a previous session" % tot, file=sys.stderr)


def drain(pl, dm, every=1, stop=lambda: False):
    """s2mm runs of up to 8 records / 20 ms idle; re-framed on the record magic"""
    n, pend, last_stat, batch = 0, [], 0.0, 8 * REC_WORDS
    while not stop():
        start(pl, S2MM, RESV_PA + CSI_OFF, batch, idle_s=0.02)
        wait(pl, S2MM, 5)
        got = s2mm_count(dm, CSI_OFF, batch)
        pend += struct.unpack("<%dI" % got, dm.m[CSI_OFF:CSI_OFF + got * 4])
        i = 0
        while len(pend) - i >= REC_WORDS:
            if pend[i] != REC_MAGIC:
                i += 1
                continue
            if n % every == 0:
                print(csi_line(pend[i:i + REC_WORDS]), flush=True)
            n += 1
            i += REC_WORDS
        del pend[:i]
        if time.time() - last_stat > 1.0:
            print(stat_line(pl), flush=True)
            last_stat = time.time()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["status", "selftest", "play", "live"])
    ap.add_argument("vecdir", nargs="?")
    ap.add_argument("--chan", type=int, default=6)
    ap.add_argument("--loop", action="store_true")
    ap.add_argument("--csi-every", type=int, default=1)
    a = ap.parse_args()
    pl = Mem(PL_BASE, PL_SPAN)
    if a.cmd == "status":
        print(stat_line(pl))
        return 0
    dm = Mem(RESV_PA, RESV_SPAN)
    pl.w(IQ_MUX + 0x10, 0x3)                      # park RF (playback + drain), then purge
    purge(pl, dm)
    if a.cmd == "live":
        off = (2407 + 5 * a.chan - 2437) * 1e6
        configure(pl, int(round(-off / 80e6 * 2 ** 32)) & 0xFFFFFFFF, playback=False)
        try:
            drain(pl, dm, a.csi_every)
        except KeyboardInterrupt:
            pass
        pl.w(IQ_MUX + 0x10, 0x3)
        return 0
    meta = json.load(open(os.path.join(a.vecdir, "vectors.json")))
    iq = load_iq(a.vecdir)
    dm.m[IQ_OFF:IQ_OFF + len(iq)] = iq
    configure(pl, meta["nco_inc"], playback=True)
    if a.cmd == "selftest":
        with open(os.path.join(a.vecdir, "csi_rec.bin"), "rb") as f:
            gold = f.read()
        nrec = len(gold) // (REC_WORDS * 4)
        cap = nrec * REC_WORDS + 64 * REC_WORDS   # slack: extra records are reported
        start(pl, S2MM, RESV_PA + CSI_OFF, cap, idle_s=1.0)
        start(pl, MM2S, RESV_PA + IQ_OFF, len(iq) // 4)
        ok = wait(pl, MM2S, 60) and wait(pl, S2MM, 30)
        n = s2mm_count(dm, CSI_OFF, cap)
        got = dm.m[CSI_OFF:CSI_OFF + len(gold)]
        bad = sum(1 for i in range(0, len(gold), 4) if got[i:i + 4] != gold[i:i + 4])
        ok = ok and n == nrec * REC_WORDS and bad == 0
        print(stat_line(pl))
        print("CSI records: %d expected, %d received, %d words differ from golden -> %s"
              % (nrec, n // REC_WORDS, bad, "PASS" if ok else "FAIL"))
        return 0 if ok else 1
    # play: paced by chunks.txt (every 20 packets = 1 s of scenario time)
    import threading
    stop = [False]
    th = threading.Thread(target=drain, args=(pl, dm, a.csi_every, lambda: stop[0]), daemon=True)
    th.start()
    cuts = [0] + [int(x) + PREROLL for x in open(os.path.join(a.vecdir, "chunks.txt")).read().split() if int(x)] + [len(iq) // 4]
    while True:
        t0 = time.time()
        for k in range(len(cuts) - 1):
            start(pl, MM2S, RESV_PA + IQ_OFF + cuts[k] * 4, cuts[k + 1] - cuts[k])
            wait(pl, MM2S, 10)
            time.sleep(max(0.0, t0 + (k + 1) * 20.0 / meta["pkt_rate"] - time.time()))
        if not a.loop:
            break
    time.sleep(2)
    stop[0] = True
    return 0


if __name__ == "__main__":
    sys.exit(main())
