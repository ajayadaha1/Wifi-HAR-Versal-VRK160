#!/usr/bin/env python3
"""live_dashboard.py - VRK160 WiFi-CSI Human-Activity-Recognition live dashboard.

The VRK160 receives WiFi itself (RF-ADC -> PL OFDM receiver -> CSI) and the AI
Engine extracts the features; this page shows both halves live:

  * activity / presence / fall / breathing rate  (from the AIE feature records)
  * motion-power history and the breathing spectrum (AIE FIR->stats, windowed DFT)
  * CSI waterfall |H[k]| over time + the latest CSI snapshot  (OFDM receiver)
  * receiver statistics: packets/s, L-SIG valid %, rate mix, CFO, SNR, RSSI, ADC status

Pure Python 3 standard library (http.server + Server-Sent Events + a hand-written
canvas renderer), no CDN / no JS libraries -> runs on the PetaLinux rootfs.
Routes: /  /stream (SSE)  /api/state  /healthz

Sources (--source):
  exec   : run the XRT host and parse its FEAT/CSI/STAT lines, e.g.
           --cmd "./host_wifi play wifi_har_vrk160.xclbin vectors --loop"   (board, no RF)
           --cmd "./host_wifi live wifi_har_vrk160.xclbin --chan 6"         (board, RF)
  replay : replay a golden_chain output directory (feat.bin + csi_rec.bin) at the
           scenario's real-time pace - the full demo on any PC, no board
  fifo   : read the same lines from a file / named pipe (--fifo path)

Thresholds are calibrated on the golden activity scenario (docs/PROJECT_STATE.md).
"""
import argparse
import json
import math
import os
import queue
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "csi_ref"))
try:
    from features8 import MOTION_GAIN, PHASEVAR_NORM, PRESENCE_GAIN, features8_vector
except ImportError:                                   # copied next to this script on target
    sys.exit("live_dashboard: features8.py not found (copy csi_ref/features8.py next to this script)")

# ---- calibrated thresholds (AIE outputs; see docs/PROJECT_STATE.md "calibration") ----
MOTION_ACTIVE = 3e-3      # walking 2.8e-2 | breathing 1.1e-5 | lying 5.5e-6 | empty 7.6e-7
MOTION_MICRO = 2e-6       # someone present but still (micro-motion)
BR_SNR_TH = 6.0           # breathing peak / out-of-band median (breathing 73, lying 12)
BR_LO_HZ, BR_HI_HZ = 0.10, 0.60
BR_DEC = 10               # csi_feat br_dec (breathing branch fs = fs_csi / BR_DEC)
# Fall = stillness -> a SHORT motion burst -> stillness while still present. A 1 s fall
# inside the 12.8 s AIE motion window shows as a ~1.7e-3 plateau lasting one window
# length (<= FALL_MAX_BURST hops); walking bursts last longer and go higher. The
# alert therefore fires ~13 s after the event (when the burst has left the window).
FALL_LO, FALL_BURST, FALL_PRE, FALL_MAX_BURST, FALL_AFTER, FALL_HOLD = 1e-4, 5e-4, 10, 32, 3, 20.0
HISTORY = 240             # feature windows kept for late browsers
CSI_KEEP = 160            # CSI snapshots kept for the waterfall
STALE_S = 3.0

N_MOT, N_BRT, N_PHS = 3, 33, 3
REC_WORDS, REC_HDR, FEAT_WORDS, TAG_WORDS = 144, 16, 52, 12


def _median(v):
    s = sorted(v)
    if not s:
        return 0.0
    h = len(s) // 2
    return s[h] if len(s) % 2 else 0.5 * (s[h - 1] + s[h])


def dbfs(p):
    return 10.0 * math.log10(max(p, 1e-3) / (32768.0 * 32768.0))


# ---- record -> line formatting (identical to aie/host/host_wifi.cpp) ------------------
def _f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def feat_line(w):
    t = ((w[3] << 32) | w[2]) / 20e6
    head = "FEAT,%u,%.6f,%u,%u,%.1f,%.1f,%.3f,%.0f" % (
        w[1], t, w[4] & 0xFFFFFF, w[4] >> 31, dbfs(_f(w[5])),
        10 * math.log10(max(1e-6, _f(w[6]))), _f(w[7]), _f(w[11]))
    return head + "".join(",%.6g" % _f(w[TAG_WORDS + i]) for i in range(N_MOT + N_BRT + N_PHS))


def csi_line(w):
    t = ((w[3] << 32) | w[2]) / 20e6
    head = "CSI,%u,%.6f,%u,%u,%u,%.0f,%.1f,%.1f" % (
        w[1], t, (w[4] >> 24) & 1, (w[12] >> 16) & 0xFF, w[13], _f(w[5]), dbfs(_f(w[6])),
        10 * math.log10(max(1e-6, _f(w[7]))))
    amps = []
    for kk in range(-32, 32):
        k = kk & 63
        re, im = _f(w[REC_HDR + 2 * k]), _f(w[REC_HDR + 2 * k + 1])
        amps.append(",%.4g" % math.hypot(re, im))
    return head + "".join(amps)


def _words(path, n):
    with open(path, "rb") as f:
        data = f.read()
    cnt = len(data) // (4 * n)
    return [struct.unpack_from("<%dI" % n, data, i * 4 * n) for i in range(cnt)]


# ---- feature sources ----------------------------------------------------------------
def source_replay(d, speed=1.0, loop=True, hop=10):
    """golden_chain output dir -> interleaved CSI (every packet) / FEAT lines at the
    scenario pace: packet i at i/fs, feature window k after packet 255 + k*hop."""
    feats = _words(os.path.join(d, "feat.bin"), FEAT_WORDS)
    recs = _words(os.path.join(d, "csi_rec.bin"), REC_WORDS)
    fs = _f(feats[0][7]) if feats else 20.0
    while True:
        t0, fi = time.time(), 0
        for i, r in enumerate(recs):
            yield csi_line(r)
            while fi < len(feats) and 255 + feats[fi][1] * hop <= i:
                yield feat_line(feats[fi])
                fi += 1
            if i % 20 == 0:
                yield "STAT,%u,%u,%u,%u,0,%u,%u,-50.0" % (0, i + 1, i + 1, i + 1, i + 1, fi)
            time.sleep(max(0.0, t0 + (i + 1) / fs / speed - time.time()))
        if not loop:
            return


def source_exec(cmd, restart=True):
    while True:
        try:
            p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, text=True, bufsize=1)
        except OSError as e:
            print("[dash] cannot start %r: %s" % (cmd, e), file=sys.stderr)
            p = None
        if p is not None:
            try:
                for line in p.stdout:
                    yield line.strip()
            finally:
                p.terminate()
        if not restart:
            return
        print("[dash] host exited - restarting", file=sys.stderr)
        time.sleep(1.0)


def source_fifo(path):
    while not os.path.exists(path):
        time.sleep(0.2)
    with open(path) as f:
        while True:
            line = f.readline()
            if not line:
                time.sleep(0.05)
                continue
            yield line.strip()


# ---- metrics --------------------------------------------------------------------------
def breathing_from_bins(brt, fs_b):
    """33 DFT bins (64-pt, fs_b) -> (bpm, snr, peak_bin, lo, hi, bin_hz), parabolic peak."""
    n = len(brt)
    bin_hz = fs_b / 64.0
    lo = max(1, int(math.ceil(BR_LO_HZ / bin_hz)))
    hi = min(n - 2, int(math.floor(BR_HI_HZ / bin_hz)))
    if hi < lo or not any(brt):
        return 0.0, 0.0, -1, lo, hi, bin_hz
    pk = max(range(lo, hi + 1), key=lambda i: brt[i])
    pos = float(pk)
    a, b, c = brt[pk - 1], brt[pk], brt[pk + 1]
    den = a - 2.0 * b + c
    if den < 0.0:
        pos += max(-0.5, min(0.5, 0.5 * (a - c) / den))
    floor = [brt[i] for i in range(1, n) if abs(i - pk) > 1]
    med = _median(floor) or 1e-12
    return pos * bin_hz * 60.0, brt[pk] / med, pk, lo, hi, bin_hz


class Hub:
    def __init__(self, source):
        self.lock = threading.Lock()
        self.clients = set()
        self.source = source
        self.started = time.time()
        self.feat_hist = []
        self.csi_hist = []
        self.last_feat = None
        self.last_stat = None
        self.last_t = 0.0
        self.n_feat = 0
        self.n_csi = 0
        self.bpm_hist = []
        self.fall_until = 0.0
        self.pre_still = 0          # consecutive still windows before a burst
        self.burst_n = 0            # windows in the current burst
        self.burst_pre = 0
        self.low_after = 0
        self.pkt_times = []
        self.sig_ok = []
        self.rates = {}

    # -- ingest ------------------------------------------------------------------
    def on_line(self, line):
        parts = line.split(",")
        try:
            if parts[0] == "FEAT" and len(parts) >= 9 + N_MOT + N_BRT + N_PHS:
                self._feat([float(x) for x in parts[1:]])
            elif parts[0] == "CSI" and len(parts) >= 9 + 64:
                self._csi([float(x) for x in parts[1:]])
            elif parts[0] == "STAT" and len(parts) >= 9:
                self._stat([float(x) for x in parts[1:]])
        except ValueError:
            pass

    def _feat(self, v):
        seq, t, npk, brt_full, rssi, snr, fs, cfo = v[:8]
        mot = v[8:8 + N_MOT]
        brt = [abs(x) for x in v[8 + N_MOT:8 + N_MOT + N_BRT]]
        phs = v[8 + N_MOT + N_BRT:8 + N_MOT + N_BRT + N_PHS]
        now = time.time()
        motion = abs(mot[2])
        phase_var = abs(phs[1])
        fs_b = (fs or 20.0) / BR_DEC
        bpm_raw, br_snr, pk, lo, hi, bin_hz = breathing_from_bins(brt, fs_b)
        breathing_ok = bool(brt_full) and br_snr > BR_SNR_TH and motion < MOTION_ACTIVE
        if breathing_ok:
            self.bpm_hist.append(bpm_raw)
            del self.bpm_hist[:-5]
        bpm = _median(self.bpm_hist) if breathing_ok else 0.0
        present = motion > MOTION_MICRO or breathing_ok
        if motion < FALL_LO:
            if self.burst_n:
                self.low_after += 1
                if self.low_after >= FALL_AFTER:
                    if self.burst_pre >= FALL_PRE and self.burst_n <= FALL_MAX_BURST and present:
                        self.fall_until = now + FALL_HOLD
                    self.burst_n = self.low_after = 0
                    self.pre_still = FALL_AFTER
            else:
                self.pre_still += 1
        elif motion > FALL_BURST:
            if self.burst_n == 0:
                self.burst_pre = self.pre_still
            self.burst_n += 1
            self.low_after = 0
            self.pre_still = 0
        fall = now < self.fall_until
        if fall:
            activity = "FALL - person down"
        elif motion > MOTION_ACTIVE:
            activity = "walking / active"
        elif breathing_ok:
            activity = "still - breathing"
        elif present:
            activity = "present - still"
        else:
            activity = "empty room"
        raw8 = [PRESENCE_GAIN * (1.0 if present else 0.0), MOTION_GAIN * motion, bpm, 0.0,
                phase_var / PHASEVAR_NORM, 1.0 if present else 0.0, 1.0 if fall else 0.0, rssi]
        f8 = features8_vector(raw8[0], raw8[1], raw8[2], raw8[3], raw8[4], raw8[5], fall, raw8[7])
        msg = {"type": "feat", "t": round(now, 3), "seq": int(seq), "t_rx": round(t, 3),
               "n_pkts": int(npk), "brt_full": bool(brt_full), "rssi": rssi, "snr": snr, "fs": fs,
               "cfo": cfo, "motion": motion, "phase_var": round(phase_var, 5),
               "bpm": round(bpm, 1), "bpm_raw": round(bpm_raw, 1), "br_snr": round(br_snr, 1),
               "breathing_ok": breathing_ok, "present": present, "fall": fall,
               "fall_left": round(max(0.0, self.fall_until - now), 1), "activity": activity,
               "spectrum": [round(x, 5) for x in brt], "peak_bin": pk, "band": [lo, hi],
               "bin_hz": bin_hz, "mot": mot, "phs": phs, "features8": [round(x, 4) for x in f8]}
        with self.lock:
            self.last_feat = msg
            self.last_t = now
            self.n_feat += 1
            self.feat_hist.append({k: msg[k] for k in ("t", "motion", "phase_var", "bpm", "activity")})
            del self.feat_hist[:-HISTORY]
        self._push(msg)

    def _csi(self, v):
        seq, t, sig_ok, rate, length, cfo, rssi, snr = v[:8]
        amps = v[8:8 + 64]
        now = time.time()
        self.pkt_times.append(now)
        self.sig_ok.append(int(sig_ok))
        cut = now - 5.0
        while self.pkt_times and self.pkt_times[0] < cut:
            self.pkt_times.pop(0)
            self.sig_ok.pop(0)
        if sig_ok:
            self.rates[int(rate)] = self.rates.get(int(rate), 0) + 1
        mx = max(amps) or 1.0
        snap = [round(a / mx, 3) for a in amps]
        msg = {"type": "csi", "seq": int(seq), "t_rx": round(t, 3), "sig_ok": bool(sig_ok),
               "rate": int(rate), "len": int(length), "cfo": cfo, "rssi": rssi, "snr": snr,
               "amp": snap, "pps": round(len(self.pkt_times) / 5.0, 1),
               "sig_pct": round(100.0 * sum(self.sig_ok) / max(1, len(self.sig_ok)), 1),
               "rates": self.rates}
        with self.lock:
            self.n_csi += 1
            self.last_t = now
            self.csi_hist.append(snap)
            del self.csi_hist[:-CSI_KEEP]
        self._push(msg)

    def _stat(self, v):
        msg = {"type": "stat", "rf_count": int(v[0]), "n_trig": int(v[1]), "n_pkt": int(v[2]),
               "n_sig_ok": int(v[3]), "n_resync": int(v[4]), "feat_in": int(v[5]),
               "feat_win": int(v[6]), "p_now": v[7], "t": time.time()}
        with self.lock:
            prev = self.last_stat
            if prev:
                dt = max(1e-3, msg["t"] - prev["t"])
                msg["adc_msps"] = round(((msg["rf_count"] - prev["rf_count"]) & 0xFFFFFFFF) / dt / 1e6, 2)
            self.last_stat = msg
        self._push(msg)

    # -- fan-out -----------------------------------------------------------------
    def _push(self, msg):
        with self.lock:
            for q in list(self.clients):
                try:
                    q.put_nowait(msg)
                except queue.Full:
                    self.clients.discard(q)

    def subscribe(self):
        q = queue.Queue(maxsize=256)
        with self.lock:
            self.clients.add(q)
        return q

    def unsubscribe(self, q):
        with self.lock:
            self.clients.discard(q)

    def state(self):
        age = time.time() - self.last_t if self.last_t else None
        status = "waiting" if age is None else ("live" if age < STALE_S else "stalled")
        return {"status": status, "age": None if age is None else round(age, 1), "source": self.source,
                "n_feat": self.n_feat, "n_csi": self.n_csi, "uptime": round(time.time() - self.started, 1)}


def make_handler(hub):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, body, ctype):
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            try:
                if self.path in ("/", "/index.html"):
                    self._send(HTML.encode(), "text/html; charset=utf-8")
                elif self.path.startswith("/api/state"):
                    st = hub.state()
                    st.update(last_feat=hub.last_feat, last_stat=hub.last_stat)
                    self._send(json.dumps(st).encode(), "application/json")
                elif self.path.startswith("/healthz"):
                    st = hub.state()
                    self._send(("status=%s age=%s feat=%d csi=%d source=%s\n" % (
                        st["status"], st["age"], st["n_feat"], st["n_csi"], st["source"])).encode(),
                        "text/plain")
                elif self.path == "/stream":
                    self._stream()
                else:
                    self.send_error(404)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def _ev(self, name, payload):
            self.wfile.write(("event: %s\ndata: %s\n\n" % (name, json.dumps(payload))).encode())
            self.wfile.flush()

        def _stream(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            q = hub.subscribe()
            try:
                self.wfile.write(b"retry: 1500\n\n")
                with hub.lock:
                    init = {"state": hub.state(), "feat_hist": list(hub.feat_hist),
                            "csi_hist": list(hub.csi_hist), "last_feat": hub.last_feat,
                            "last_stat": hub.last_stat,
                            "cfg": {"motion_active": MOTION_ACTIVE, "motion_micro": MOTION_MICRO,
                                    "br_snr": BR_SNR_TH, "stale": STALE_S}}
                self._ev("init", init)
                while True:
                    try:
                        msg = q.get(timeout=1.0)
                    except queue.Empty:
                        self._ev("hb", hub.state())
                        continue
                    self._ev(msg["type"], msg)
            except (BrokenPipeError, ConnectionResetError, OSError, ValueError):
                pass
            finally:
                hub.unsubscribe(q)
    return H


HTML = r"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>VRK160 WiFi-CSI HAR - live</title>
<style>
:root{--bg:#0b0e14;--panel:#12161d;--line:#232a36;--ink:#fff;--mut:#8b93a1;--blue:#3987e5;
 --aqua:#199e70;--orange:#d95926;--good:#0ca30c;--warn:#fab219;--crit:#d03b3b}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:15px ui-sans-serif,system-ui,'Segoe UI',Roboto,Arial,sans-serif}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:12px 22px;
 background:var(--panel);border-bottom:1px solid var(--line)}
h1{margin:0;font-size:clamp(17px,1.7vw,25px);font-weight:650}.sub{color:var(--mut);font-size:13px;margin-top:3px}
.pill{font-weight:700;letter-spacing:.08em;padding:6px 14px;border-radius:999px;border:2px solid;white-space:nowrap}
.pill.good{color:var(--good);border-color:var(--good)}.pill.warn{color:var(--warn);border-color:var(--warn)}
.pill.crit{color:#fff;background:var(--crit);border-color:var(--crit)}
.meta{color:var(--mut);font:12px ui-monospace,Menlo,Consolas,monospace;text-align:right}
.alert{background:var(--crit);color:#fff;font-weight:800;font-size:clamp(20px,2.6vw,38px);letter-spacing:.1em;padding:10px 22px}
main{padding:16px 22px;display:flex;flex-direction:column;gap:14px}
.tiles{display:grid;grid-template-columns:repeat(6,1fr);gap:14px}
.tile{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:12px 16px;min-width:0}
.k{font-size:12px;text-transform:uppercase;letter-spacing:.12em;color:var(--mut);font-weight:600}
.v{font-size:clamp(22px,2.6vw,42px);font-weight:700;margin-top:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.v.sm{font-size:clamp(17px,1.8vw,28px)}.u{font-size:.45em;color:var(--mut);margin-left:.3em}
.s{margin-top:3px;color:var(--mut);font:12px ui-monospace,Menlo,Consolas,monospace;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#t_act.fall{background:var(--crit)}#v_act{white-space:normal;line-height:1.1;min-height:2.2em}
.row{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:10px 14px}
.ph{display:flex;align-items:baseline;gap:10px;font-weight:650;padding-bottom:6px}.ph b{margin-left:auto;font:13px ui-monospace,monospace}
.ph span{font-weight:400;color:var(--mut);font-size:.85em}
.cw{position:relative;height:24vh;min-height:170px}canvas{position:absolute;inset:0;width:100%;height:100%}
table{width:100%;border-collapse:collapse;font-size:14px}td{padding:3px 6px;border-top:1px solid var(--line)}
td.v2{text-align:right;font:600 13px ui-monospace,monospace}.bar{height:11px;border-radius:3px;background:#1d2530;overflow:hidden}
.bar i{display:block;height:100%;background:var(--blue)}
@media (max-width:1100px){.tiles{grid-template-columns:repeat(3,1fr)}.row{grid-template-columns:1fr}}
</style></head><body>
<header><div><h1>VRK160 WiFi-CSI HAR &mdash; OFDM receiver &rarr; AI Engine</h1>
<div class=sub>Versal RF (xcvr1602): RF-ADC 6.4 GSPS &rarr; PL 802.11 OFDM RX (CSI) &rarr; AIE feature graph &rarr; this page</div></div>
<div style="display:flex;gap:14px;align-items:center"><div class=meta id=meta>connecting&hellip;</div><div class="pill warn" id=pill>WAITING</div></div></header>
<div class=alert id=alert hidden>&#9888; FALL DETECTED &mdash; person down <span id=fall_left></span></div>
<main>
<section class=tiles>
 <div class=tile id=t_act><div class=k>Activity</div><div class="v sm" id=v_act>&ndash;</div><div class=s id=s_act>&nbsp;</div></div>
 <div class=tile><div class=k>Presence</div><div class=v id=v_pres>&ndash;</div><div class=s id=s_pres>&nbsp;</div></div>
 <div class=tile><div class=k>Breathing</div><div class=v id=v_bpm>&ndash;</div><div class=s id=s_bpm>&nbsp;</div></div>
 <div class=tile><div class=k>Motion power</div><div class="v sm" id=v_mot>&ndash;</div><div class=s id=s_mot>AIE FIR&rarr;stats</div></div>
 <div class=tile><div class=k>Packets</div><div class=v id=v_pps>&ndash;</div><div class=s id=s_pps>&nbsp;</div></div>
 <div class=tile><div class=k>Link</div><div class="v sm" id=v_link>&ndash;</div><div class=s id=s_link>&nbsp;</div></div>
</section>
<section class=row>
 <div class=panel><div class=ph>Motion power <span>AIE motion branch, log scale</span><b id=b_mot></b></div><div class=cw><canvas id=c_mot></canvas></div></div>
 <div class=panel><div class=ph>Breathing spectrum <span>AIE windowed DFT, 64 pt</span><b id=b_brt></b></div><div class=cw><canvas id=c_brt></canvas></div></div>
</section>
<section class=row>
 <div class=panel><div class=ph>CSI waterfall <span>|H[k]|, subcarriers &minus;26..26 &times; packets</span><b id=b_wf></b></div><div class=cw><canvas id=c_wf></canvas></div></div>
 <div class=panel><div class=ph>Latest CSI <span>802.11 L-LTF channel estimate</span><b id=b_csi></b></div><div class=cw><canvas id=c_csi></canvas></div></div>
</section>
<section class=row>
 <div class=panel><div class=ph>OFDM receiver <span>PL wifi_ddc / ofdm_fe / ofdm_be</span></div><table id=rx></table></div>
 <div class=panel><div class=ph>ruview 8-feature vector <span>encoder input</span></div><table id=f8></table></div>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
let cfg={motion_active:3e-3,motion_micro:2e-6,br_snr:6,stale:3};
let motH=[],wf=[],lastCsi=null,lastFeat=null,lastStat=null,lastMsg=Date.now(),src='',nF=0,nC=0,t0=Date.now();
const F8=['presence','motion','breathing','heart rate','phase var','persons','fall','rssi'];
function fit(c){const r=c.getBoundingClientRect(),d=window.devicePixelRatio||1;
 if(c.width!=Math.round(r.width*d)||c.height!=Math.round(r.height*d)){c.width=Math.round(r.width*d);c.height=Math.round(r.height*d)}
 const x=c.getContext('2d');x.setTransform(d,0,0,d,0,0);return [x,r.width,r.height]}
function axes(x,w,h){x.clearRect(0,0,w,h);x.strokeStyle='#232a36';x.lineWidth=1;
 for(let i=1;i<4;i++){x.beginPath();x.moveTo(0,h*i/4);x.lineTo(w,h*i/4);x.stroke()}}
function drawMot(){const [x,w,h]=fit($('c_mot'));axes(x,w,h);const lo=-8,hi=-0.5;
 const y=v=>h-(Math.log10(Math.max(v,1e-9))-lo)/(hi-lo)*h;
 [[cfg.motion_active,'#d95926','active'],[cfg.motion_micro,'#fab219','micro']].forEach(([v,c,l])=>{
  x.strokeStyle=c;x.setLineDash([5,4]);x.beginPath();x.moveTo(0,y(v));x.lineTo(w,y(v));x.stroke();x.setLineDash([]);
  x.fillStyle=c;x.font='11px monospace';x.fillText(l,4,y(v)-3)});
 if(motH.length<2)return;x.strokeStyle='#3987e5';x.lineWidth=2;x.beginPath();
 motH.forEach((v,i)=>{const px=i/(motH.length-1)*w;i?x.lineTo(px,y(v)):x.moveTo(px,y(v))});x.stroke()}
function drawBrt(f){const [x,w,h]=fit($('c_brt'));axes(x,w,h);if(!f)return;const s=f.spectrum,n=s.length;
 const mx=Math.max(...s.slice(1))||1,bw=w/n;
 x.fillStyle='rgba(25,158,112,.12)';x.fillRect(f.band[0]*bw,0,(f.band[1]-f.band[0]+1)*bw,h);
 s.forEach((v,i)=>{x.fillStyle=i==f.peak_bin?(f.breathing_ok?'#0ca30c':'#fab219'):(i==0?'#39414f':'#3987e5');
  const bh=Math.min(1,v/mx)*(h-14);x.fillRect(i*bw+1,h-bh,bw-2,bh)});
 x.fillStyle='#8b93a1';x.font='11px monospace';
 for(let i=0;i<n;i+=8)x.fillText((i*f.bin_hz*60).toFixed(0),i*bw+2,11)}
function heat(v){v=Math.max(0,Math.min(1,v));const r=Math.round(255*Math.min(1,Math.max(0,1.5*v-0.3))),
 g=Math.round(255*Math.min(1,2*v)*(v<0.85?1:0.9)),b=Math.round(255*Math.max(0,1-2.2*v)+60*v);return [r,g,b]}
function drawWf(){const c=$('c_wf'),[x,w,h]=fit(c);x.clearRect(0,0,w,h);if(!wf.length)return;
 const K=53,img=x.createImageData(wf.length,K);
 wf.forEach((a,t)=>{for(let k=0;k<K;k++){const v=a[k+6],[r,g,b]=heat(v),o=(k*wf.length+t)*4;
  img.data[o]=r;img.data[o+1]=g;img.data[o+2]=b;img.data[o+3]=255}});
 const tmp=document.createElement('canvas');tmp.width=wf.length;tmp.height=K;tmp.getContext('2d').putImageData(img,0,0);
 x.imageSmoothingEnabled=false;x.drawImage(tmp,0,0,w,h)}
function drawCsi(){const [x,w,h]=fit($('c_csi'));axes(x,w,h);if(!lastCsi)return;const a=lastCsi.amp.slice(6,59);
 x.strokeStyle='#199e70';x.lineWidth=2;x.beginPath();a.forEach((v,i)=>{const px=i/(a.length-1)*w,py=h-v*(h-10);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke();
 x.fillStyle='#8b93a1';x.font='11px monospace';x.fillText('-26',2,h-3);x.fillText('0',w/2-4,h-3);x.fillText('+26',w-24,h-3)}
function rxTable(){const s=lastStat||{},c=lastCsi||{},f=lastFeat||{};
 const rates=c.rates?Object.entries(c.rates).sort((a,b)=>a[0]-b[0]).map(([r,n])=>r+'M:'+n).join(' '):'-';
 const rows=[['triggers (L-STF)',s.n_trig],['packets decoded',s.n_pkt],['L-SIG valid',s.n_sig_ok],
  ['resyncs',s.n_resync],['CSI -> csi_feat',s.feat_in],['AIE windows',s.feat_win],
  ['ADC stream',(s.adc_msps>0.01)?(s.adc_msps+' MSPS (RF live)'):(s.rf_count?'stopped':'idle (playback)')],
  ['input power',s.p_now!=null?s.p_now.toFixed(1)+' dBFS':'-'],['last CFO',c.cfo!=null?(c.cfo/1e3).toFixed(1)+' kHz':'-'],
  ['last rate / len',c.rate!=null?c.rate+' Mb/s / '+c.len+' B':'-'],['rate mix',rates]];
 $('rx').innerHTML=rows.map(([k,v])=>'<tr><td>'+k+'</td><td class=v2>'+(v==null?'-':v)+'</td></tr>').join('')}
function f8Table(){if(!lastFeat)return;$('f8').innerHTML=lastFeat.features8.map((v,i)=>'<tr><td>'+F8[i]+
 '</td><td style="width:55%"><div class=bar><i style="width:'+(100*v).toFixed(0)+'%"></i></div></td><td class=v2>'+v.toFixed(3)+'</td></tr>').join('')}
function onFeat(f){lastFeat=f;motH.push(f.motion);motH=motH.slice(-240);
 $('v_act').textContent=f.activity;$('t_act').classList.toggle('fall',f.fall);
 $('s_act').textContent='window #'+f.seq+' | '+f.n_pkts+' pkts';
 $('v_pres').textContent=f.present?'YES':'no';$('v_pres').style.color=f.present?'var(--good)':'var(--mut)';
 $('s_pres').textContent='phase var '+f.phase_var.toFixed(4);
 $('v_bpm').innerHTML=f.breathing_ok?f.bpm.toFixed(1)+'<span class=u>bpm</span>':'&ndash;';
 $('s_bpm').textContent=f.brt_full?('SNR '+f.br_snr.toFixed(1)+' (raw '+f.bpm_raw.toFixed(1)+')'):'warming up (32 s)';
 $('v_mot').textContent=f.motion.toExponential(2);$('b_mot').textContent=f.motion.toExponential(2);
 $('b_brt').textContent=f.peak_bin>=0?(f.bpm_raw.toFixed(1)+' bpm'):'';
 $('alert').hidden=!f.fall;$('fall_left').textContent=f.fall?'('+f.fall_left+' s)':'';
 drawMot();drawBrt(f);f8Table()}
function onCsi(c){lastCsi=c;wf.push(c.amp);wf=wf.slice(-160);
 $('v_pps').innerHTML=c.pps.toFixed(1)+'<span class=u>/s</span>';$('s_pps').textContent='L-SIG ok '+c.sig_pct+'% | seq '+c.seq;
 $('v_link').textContent=c.rssi.toFixed(0)+' dBFS';$('s_link').textContent='SNR '+c.snr.toFixed(1)+' dB | CFO '+(c.cfo/1e3).toFixed(1)+' kHz';
 $('b_csi').textContent=(c.sig_ok?'L-SIG ok':'L-SIG bad')+' | '+c.rate+' Mb/s';$('b_wf').textContent=wf.length+' pkts';
 drawWf();drawCsi();rxTable()}
function status(st){src=st.source||src;const age=(Date.now()-lastMsg)/1000,p=$('pill');
 if(!st||st.status=='waiting'){p.className='pill warn';p.textContent='WAITING'}
 else if(age>cfg.stale||st.status=='stalled'){p.className='pill crit';p.textContent='NO DATA'}
 else{p.className='pill good';p.textContent='LIVE'}
 $('meta').innerHTML='source '+src+'<br>windows '+nF+' | csi '+nC+' | up '+((Date.now()-t0)/1000).toFixed(0)+' s'}
const es=new EventSource('/stream');
es.addEventListener('init',e=>{const d=JSON.parse(e.data);cfg=Object.assign(cfg,d.cfg);nF=d.state.n_feat||0;nC=d.state.n_csi||0;
 motH=d.feat_hist.map(f=>f.motion);wf=d.csi_hist;lastStat=d.last_stat;if(d.last_feat)onFeat(d.last_feat);drawWf();rxTable();status(d.state)});
es.addEventListener('feat',e=>{lastMsg=Date.now();nF++;onFeat(JSON.parse(e.data))});
es.addEventListener('csi',e=>{lastMsg=Date.now();nC++;onCsi(JSON.parse(e.data))});
es.addEventListener('stat',e=>{lastStat=JSON.parse(e.data);rxTable()});
es.addEventListener('hb',e=>status(JSON.parse(e.data)));
window.addEventListener('resize',()=>{drawMot();drawBrt(lastFeat);drawWf();drawCsi()});
setInterval(()=>status({status:(Date.now()-lastMsg)/1000>cfg.stale?'stalled':'live',source:src}),1000);
</script></body></html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", choices=["exec", "replay", "fifo"], default="replay")
    ap.add_argument("--cmd", help="exec: host command line")
    ap.add_argument("--dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "vectors", "demo"),
                    help="replay: golden_chain output dir (default: <repo>/build/vectors/demo)")
    ap.add_argument("--fifo", help="fifo: file / named pipe with host lines")
    ap.add_argument("--speed", type=float, default=1.0, help="replay speed factor")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--host", default="0.0.0.0")
    a = ap.parse_args()
    if a.source == "exec":
        if not a.cmd:
            sys.exit("--source exec needs --cmd")
        gen, label = source_exec(a.cmd), "exec"
    elif a.source == "fifo":
        if not a.fifo:
            sys.exit("--source fifo needs --fifo")
        gen, label = source_fifo(a.fifo), "fifo"
    else:
        gen, label = source_replay(a.dir, a.speed), "replay"
    hub = Hub(label)

    def pump():
        for line in gen:
            hub.on_line(line)
    threading.Thread(target=pump, daemon=True).start()
    srv = ThreadingHTTPServer((a.host, a.port), make_handler(hub))
    srv.daemon_threads = True
    print("[live_dashboard] source=%s http://%s:%d/" % (label, a.host, a.port), flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
