#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# wifi_ofdm.py - 802.11a/g/n (legacy 20 MHz preamble) PHY golden model for the
# VRK160 WiFi-CSI receiver.
#
# Three jobs:
#   1. TX   : bit-true L-STF / L-LTF / L-SIG (+ random DATA symbols) packets.
#   2. CHAN : a human-activity multipath channel (empty / walking / breathing /
#             fall) whose per-packet frequency response is the ground-truth CSI.
#   3. RX   : a float reference receiver that implements EXACTLY the algorithm
#             the PL runs (hls/ofdm_rx: STF delay-16 autocorrelation detector,
#             segmented sign-LTF timing correlator, coarse+fine CFO, 64-pt FFT
#             channel estimate, L-SIG equalise + Viterbi). It is the independent
#             check of the HLS C++ (which is itself the bit-exact golden for the
#             hardware, see hls/README.md).
#
# Also emits the wifi_ddc decimation FIR (hls/common/ddc_taps.h) and the
# playback scenario files consumed by the HLS testbenches and the on-board
# self-test. Pure numpy/scipy; no hardware needed.
# ---------------------------------------------------------------------------
import numpy as np

FS20 = 20e6                 # 802.11 20 MHz channel sample rate
FS80 = 80e6                 # RF-DC DDC output rate on the VRK160 (6.4 GSPS / 2 / 40)
NFFT = 64
DF = FS20 / NFFT            # 312.5 kHz subcarrier spacing
C0 = 299792458.0

# ---- 802.11-2020 17.3.3 / 17.3.5 constants ----------------------------------
_STF = np.sqrt(13 / 6) * np.array(
    [0, 0, 1 + 1j, 0, 0, 0, -1 - 1j, 0, 0, 0, 1 + 1j, 0, 0, 0, -1 - 1j, 0, 0, 0, -1 - 1j,
     0, 0, 0, 1 + 1j, 0, 0, 0, 0, 0, 0, 0, -1 - 1j, 0, 0, 0, -1 - 1j, 0, 0, 0, 1 + 1j, 0, 0,
     0, 1 + 1j, 0, 0, 0, 1 + 1j, 0, 0, 0, 1 + 1j, 0, 0])
_LTF = np.array([1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1,
                 1, 1, 1, 0, 1, -1, -1, 1, 1, -1, 1, -1, 1, -1, -1, -1, -1, -1, 1, 1, -1, -1,
                 1, -1, 1, -1, 1, 1, 1, 1], dtype=float)
KS = np.arange(-26, 27)                                   # subcarrier index of the tables
USED = np.array([k for k in range(-26, 27) if k != 0])    # 52 used subcarriers
PILOT_K = np.array([-21, -7, 7, 21])
PILOT_V = np.array([1, 1, 1, -1], dtype=float)
DATA_K = np.array([k for k in USED if k not in PILOT_K])  # 48, increasing frequency
POLARITY = np.array([
    1, 1, 1, 1, -1, -1, -1, 1, -1, -1, -1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1, 1, 1, 1,
    1, 1, -1, 1, 1, 1, -1, 1, 1, -1, -1, 1, 1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, 1, -1, -1, 1,
    1, 1, 1, 1, -1, -1, 1, 1, -1, -1, 1, -1, 1, -1, 1, 1, -1, -1, -1, 1, 1, -1, -1, -1, -1, 1, -1, -1,
    1, -1, 1, 1, 1, 1, -1, 1, -1, 1, -1, 1, -1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1,
    -1, 1, -1, -1, -1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1], dtype=float)
RATE_BITS = {6: (1, 1, 0, 1), 9: (1, 1, 1, 1), 12: (0, 1, 0, 1), 18: (0, 1, 1, 1),
             24: (1, 0, 0, 1), 36: (1, 0, 1, 1), 48: (0, 0, 0, 1), 54: (0, 0, 1, 1)}
RATE_FROM_CODE = {sum(b << i for i, b in enumerate(v)): r for r, v in RATE_BITS.items()}


def ltf_freq():
    """L-LTF values on FFT bins 0..63 (bin k <-> subcarrier k mod 64)."""
    L = np.zeros(NFFT)
    L[KS % NFFT] = _LTF
    return L


def _ifft_sym(freq53):
    X = np.zeros(NFFT, complex)
    X[KS % NFFT] = freq53
    return np.fft.ifft(X) * NFFT / np.sqrt(52.0)           # unit average power


def stf_time():
    s = _ifft_sym(_STF)                                    # 16-periodic
    return np.tile(s, 3)[:160]


def ltf_T():
    return _ifft_sym(_LTF)                                 # one 64-sample LTF period


def ltf_time():
    T = ltf_T()
    return np.concatenate([T[32:], T, T])                  # GI2(32) + T1 + T2 = 160


def ltf_sign_ref():
    """Sign-quantised conj reference used by the PL timing correlator.
    Tq[i] = sgn(Re T[i]) + j sgn(Im T[i]), sgn(0)=+1. Must match hls/common."""
    T = ltf_T()
    return np.where(T.real >= 0, 1, -1), np.where(T.imag >= 0, 1, -1)


# ---- L-SIG bits / convolutional code / interleaver ---------------------------
def sig_bits(rate_mbps, length):
    b = list(RATE_BITS[rate_mbps]) + [0] + [(length >> i) & 1 for i in range(12)]
    b.append(sum(b) & 1)                                   # even parity over bits 0..16
    return np.array(b + [0] * 6, dtype=np.uint8)           # 6 tail bits


G0, G1 = 0o133, 0o171


def _tap_parity(state7, g):
    # state7 bit 6 = current input b_n, bit 6-d = b_{n-d}; g bit (6-d) selects delay d
    return bin(state7 & g).count("1") & 1


def conv_encode(bits):
    out, sr = [], 0
    for b in bits:
        sr = ((sr >> 1) | (int(b) << 6)) & 0x7F
        out += [_tap_parity(sr, G0), _tap_parity(sr, G1)]
    return np.array(out, dtype=np.uint8)


def interleave_perm(ncbps=48):
    """Position of coded bit k after the (BPSK) interleaver: i = (N/16)(k%16)+k//16."""
    k = np.arange(ncbps)
    return (ncbps // 16) * (k % 16) + k // 16


def viterbi_decode(soft, nbits):
    """Soft-decision K=7 r=1/2 Viterbi, start/end in state 0 (tail-terminated).
    soft: 2*nbits values, >0 means coded bit 1. Maximises correlation metric."""
    NS = 64
    neg = -1e30
    pm = np.full(NS, neg)
    pm[0] = 0.0
    prev = np.zeros((nbits, NS), dtype=np.int16)
    bit_of = np.zeros((nbits, NS), dtype=np.uint8)
    for t in range(nbits):
        sa, sb = soft[2 * t], soft[2 * t + 1]
        npm = np.full(NS, neg)
        for s in range(NS):                # s = previous 6 bits (b_{n-1} at bit 5)
            if pm[s] <= neg / 2:
                continue
            for b in (0, 1):
                st7 = (b << 6) | s
                ea = 1.0 if _tap_parity(st7, G0) else -1.0
                eb = 1.0 if _tap_parity(st7, G1) else -1.0
                ns = st7 >> 1
                m = pm[s] + ea * sa + eb * sb
                if m > npm[ns]:
                    npm[ns] = m
                    prev[t, ns] = s
                    bit_of[t, ns] = b
        pm = npm
    s = 0
    out = np.zeros(nbits, dtype=np.uint8)
    for t in range(nbits - 1, -1, -1):
        out[t] = bit_of[t, s]
        s = prev[t, s]
    return out


def parse_sig(bits):
    rate_code = int(sum(int(bits[i]) << i for i in range(4)))
    length = int(sum(int(bits[5 + i]) << i for i in range(12)))
    parity_ok = (int(np.sum(bits[:18])) & 1) == 0
    tail_ok = not np.any(bits[18:24])
    ok = parity_ok and tail_ok and bits[4] == 0 and rate_code in RATE_FROM_CODE and length > 0
    return dict(rate=RATE_FROM_CODE.get(rate_code, 0), rate_code=rate_code, length=length,
                parity_ok=parity_ok, tail_ok=tail_ok, ok=bool(ok))


def ofdm_symbol(data48, pilot_pol):
    X = np.zeros(NFFT, complex)
    X[DATA_K % NFFT] = data48
    X[PILOT_K % NFFT] = PILOT_V * pilot_pol
    x = np.fft.ifft(X) * NFFT / np.sqrt(52.0)
    return np.concatenate([x[-16:], x])


def make_packet(rate=6, length=100, n_data=4, rng=None):
    """Legacy (non-HT) PPDU at 20 MSPS, unit power. DATA symbols carry random
    BPSK/QPSK (content is irrelevant to CSI; they give realistic length/PAPR)."""
    rng = rng or np.random.default_rng()
    sig = conv_encode(sig_bits(rate, length))
    inter = np.zeros(48)
    inter[interleave_perm()] = 2.0 * sig - 1.0             # BPSK: bit1 -> +1
    parts = [stf_time(), ltf_time(), ofdm_symbol(inter, POLARITY[0])]
    for n in range(1, n_data + 1):
        d = (2 * rng.integers(0, 2, 48) - 1) + 1j * (2 * rng.integers(0, 2, 48) - 1)
        parts.append(ofdm_symbol(d / np.sqrt(2), POLARITY[n % 127]))
    return np.concatenate(parts)


# ---- human-activity multipath channel ----------------------------------------
class ActivityChannel:
    """Static multipath + a human-body reflection whose path length follows the
    activity. Carrier-phase term exp(-j 2 pi fc tau) is what makes mm-scale
    breathing visible in CSI, exactly as in the real room."""

    def __init__(self, fc=2.437e9, n_static=5, seed=1):
        r = np.random.default_rng(seed)
        self.fc = fc
        self.tau = np.concatenate([[30e-9], np.sort(r.uniform(50e-9, 400e-9, n_static - 1))])
        self.amp = np.exp(-self.tau / 150e-9) * r.uniform(0.6, 1.0, n_static)
        self.amp[0] = 1.0
        self.ph = r.uniform(0, 2 * np.pi, n_static)
        self.h_tau0 = 90e-9                                 # body path base delay
        self.rng = r

    def body(self, t, act):
        """(amplitude, extra path length [m]) of the body reflection at time t."""
        if act == "empty":
            return 0.0, 0.0
        if act == "breathing":                              # 15 BPM, 4 mm chest
            return 0.35, 2 * 0.004 * np.sin(2 * np.pi * 0.25 * t)
        if act == "walking":
            sway = 0.06 * np.sin(2 * np.pi * 1.8 * t) + 0.03 * np.sin(2 * np.pi * 2.6 * t + 1)
            a = 0.45 * (1 + 0.35 * np.sin(2 * np.pi * 1.8 * t + 0.5))
            return a, 2 * (0.9 * t + sway)
        if act == "fall":                                   # 0.8 s collapse
            return 0.6, 2 * 1.2 * (t / 0.8) ** 2
        if act == "lying":                                  # on the floor, shallow breathing
            return 0.25, 2 * 0.002 * np.sin(2 * np.pi * 0.3 * t) + 2.4
        raise ValueError(act)

    def taps_and_csi(self, t, act, t_act):
        """20 MSPS FIR (16 taps, band-limited delays) + its CSI on the 64 bins."""
        a_b, dl = self.body(t_act, act)
        taus = list(self.tau)
        amps = list(self.amp * np.exp(1j * self.ph))
        if a_b > 0:
            tb = self.h_tau0 + dl / C0
            taus.append(tb)
            amps.append(a_b * np.exp(1j * 1.3))
        taus = np.array(taus)
        amps = np.array(amps) * np.exp(-2j * np.pi * self.fc * taus)
        m = np.arange(16)
        h = np.zeros(16, complex)
        for tau, a in zip(taus, amps):
            d = tau * FS20
            h += a * np.sinc(m - d) * np.hanning(18)[1:17]
        H = np.fft.fft(h, NFFT)                            # CSI on FFT bins
        return h, H


# ---- 20 -> 80 MSPS "RF-DC DDC output" model -----------------------------------
def interp4(x20):
    """x4 interpolation to the 80 MSPS DDC-output domain (flat to 9 MHz)."""
    from scipy.signal import firwin, upfirdn
    h = firwin(129, 9.5e6, fs=FS80, window=("kaiser", 9.0)) * 4
    y = upfirdn(h, x20, up=4)
    return y[64: 64 + 4 * len(x20)]


def ddc_taps(ntaps=64, qbits=17):
    """wifi_ddc 80->20 MSPS decimation FIR (pass 0-8.6 MHz, stop >= 11.4 MHz),
    quantised to Q1.qbits. Symmetric, even length."""
    from scipy.signal import remez
    h = remez(ntaps, [0, 8.6e6, 11.4e6, 40e6], [1, 0], weight=[1, 12], fs=FS80)
    hq = np.round(h * (1 << qbits)).astype(np.int64)
    return hq, h


def write_ddc_header(path, ntaps=64, qbits=17):
    hq, h = ddc_taps(ntaps, qbits)
    from scipy.signal import freqz
    w, H = freqz(hq / float(1 << qbits), worN=4096, fs=FS80)
    pb = np.abs(H[w <= 8.6e6])
    sb = np.abs(H[w >= 11.4e6])
    with open(path, "w") as f:
        f.write("#pragma once\n// Generated by csi_ref/wifi_ofdm.py write_ddc_header() - do not edit.\n")
        f.write(f"// wifi_ddc 80->20 MSPS decimator: {ntaps} taps Q1.{qbits}, symmetric.\n")
        f.write(f"// passband 0-8.6 MHz ripple {20*np.log10(pb.max()/pb.min()):.3f} dB, "
                f"stopband >= 11.4 MHz {20*np.log10(sb.max()/pb.mean()):.1f} dB\n")
        f.write(f"#define DDC_NTAPS {ntaps}\n#define DDC_QBITS {qbits}\n")
        f.write("static const int DDC_TAPS[DDC_NTAPS] = {" + ", ".join(str(int(v)) for v in hq) + "};\n")
    return hq


# ---- float reference receiver (mirrors hls/ofdm_rx) ---------------------------
class RxParams:
    W = 48          # STF autocorrelation window
    DLY = 16        # STF period
    N_PLAT = 24     # consecutive metric-high samples to trigger
    THR = 0.70      # |C| >= THR * P
    PRE = 32        # capture starts PRE samples before the trigger sample
    N_CAP = 416     # capture length
    S_LO, S_HI = 40, 200     # T1 search range, capture coordinates
    ADV = 2         # FFT window advance into the cyclic prefix
    HOLDOFF = 480   # samples after a trigger with no new trigger


def rx_reference(y, p=RxParams, p_min=0.0):
    """Detect every packet in a 20 MSPS complex stream (float). Returns a list of
    dicts with csi (64 bins, zeros at nulls), cfo_hz, t1 (absolute index), sig."""
    y = np.asarray(y, complex)
    n = len(y)
    prod = np.zeros(n, complex)
    prod[p.DLY:] = y[p.DLY:] * np.conj(y[:-p.DLY])
    pw = np.abs(y) ** 2
    pwd = np.zeros(n)
    pwd[p.DLY:] = pw[:-p.DLY]
    C = np.convolve(prod, np.ones(p.W))[:n]
    P = np.convolve(pw, np.ones(p.W))[:n]
    Pd = np.convolve(pwd, np.ones(p.W))[:n]
    # correlation coefficient |C|^2/(P Pd) <= 1: packet tails into noise cannot trigger
    hi = (np.abs(C) ** 2 >= p.THR ** 2 * P * Pd) & (P > p_min)
    tr, ti = ltf_sign_ref()
    out, cnt, hold = [], 0, 0
    i = 0
    while i < n:
        if hold > 0:
            hold -= 1
            cnt = 0
            i += 1
            continue
        cnt = cnt + 1 if hi[i] else 0
        if cnt == p.N_PLAT:
            cs = i - p.PRE
            if cs >= 0 and cs + p.N_CAP <= n:
                cap = y[cs: cs + p.N_CAP]
                r = _rx_capture(cap, C[i], p, tr, ti)
                r["t_cap"] = cs
                r["t1_abs"] = cs + r["t1"]
                out.append(r)
            hold = p.HOLDOFF
            cnt = 0
        i += 1
    return out


def seg_ltf_metric(cap, tr, ti):
    """M(p) = sum_s |sum_i cap[p+16s+i] * (tr - j ti)[16s+i]|^2 (CFO-robust)."""
    L = len(cap)
    M = np.zeros(L)
    q = tr - 1j * ti
    for pos in range(L - 63):
        v = cap[pos:pos + 64] * q
        M[pos] = sum(abs(v[16 * s:16 * s + 16].sum()) ** 2 for s in range(4))
    return M


def _rx_capture(cap, Ctrig, p, tr, ti):
    M = seg_ltf_metric(cap, tr, ti)
    M2 = np.array([M[k] + M[k + 64] for k in range(len(M) - 64)])
    t1 = p.S_LO + int(np.argmax(M2[p.S_LO:p.S_HI + 1]))
    w_c = np.angle(Ctrig) / p.DLY
    R = np.sum(cap[t1 + 64:t1 + 128] * np.conj(cap[t1:t1 + 64]))
    w_f = np.angle(R * np.exp(-1j * w_c * 64)) / 64
    w = w_c + w_f
    nidx = np.arange(len(cap)) - t1
    yc = cap * np.exp(-1j * w * nidx)
    s1 = t1 - p.ADV
    Y1 = np.fft.fft(yc[s1:s1 + 64])
    Y2 = np.fft.fft(yc[s1 + 64:s1 + 128])
    Ys = np.fft.fft(yc[s1 + 128 + 16:s1 + 128 + 80])
    L = ltf_freq()
    H = 0.5 * (Y1 + Y2) * L / NFFT
    Z = Ys * np.conj(H * NFFT)
    zp = Z[PILOT_K % NFFT] * PILOT_V
    cpe = np.angle(np.sum(zp))
    Zr = Z * np.exp(-1j * cpe)
    soft_sc = Zr[DATA_K % NFFT].real
    perm = interleave_perm()
    soft = soft_sc[perm]                                   # deinterleave
    bits = viterbi_decode(soft, 24)
    noise = np.sum(np.abs(Y1[USED % NFFT] - Y2[USED % NFFT]) ** 2) / (2 * 52) / NFFT ** 2
    sigp = np.sum(np.abs(H[USED % NFFT]) ** 2) / 52
    return dict(t1=t1, cfo_hz=w * FS20 / (2 * np.pi), csi=H, sig=parse_sig(bits),
                snr_db=10 * np.log10(sigp / max(noise, 1e-30)))


# ---- scenario generator -------------------------------------------------------
# segments are longer than the AIE windows (motion 12.8 s = 256 @ 20 Hz; breathing
# 32 s = 64 @ 2 Hz) so every activity produces pure windows
SCENARIO_DEFAULT = [("empty", 20.0), ("walking", 20.0), ("breathing", 50.0),
                    ("fall", 1.0), ("lying", 40.0), ("walking", 15.0)]


def scenario(segments=SCENARIO_DEFAULT, pkt_rate=20.0, snr_db=30.0, cfo_hz=37e3,
             f_off=0.0, gap20=200, seed=7, amp=2000.0, fc=2.437e9):
    """Build a playback scenario: packets at `pkt_rate` Hz of *simulated* time,
    stored back-to-back (gap20 idle samples at 20 MSPS between packets) as
    80 MSPS int16 I/Q (the RF-DC DDC output format). Returns (iq80 int16 [N,2],
    truth dict with per-packet csi / activity / start index)."""
    rng = np.random.default_rng(seed)
    ch = ActivityChannel(fc=fc, seed=seed)
    stream, truth = [], dict(csi=[], act=[], t=[], start20=[], rate=[], length=[])
    t, pos = 0.0, 0
    for act, dur in segments:
        t0 = t
        while t < t0 + dur - 1e-9:
            rate = int(rng.choice([6, 12, 24]))
            length = int(rng.integers(40, 200))
            pk = make_packet(rate, length, n_data=3, rng=rng)
            h, H = ch.taps_and_csi(t, act, t - t0)
            rx = np.convolve(pk, h)[:len(pk) + 16]
            rx *= np.exp(1j * rng.uniform(0, 2 * np.pi))    # per-packet LO phase
            frac = int(rng.integers(0, 4))                  # 80 MSPS sub-sample start
            seg = np.concatenate([np.zeros(gap20 // 2), rx, np.zeros(gap20 - gap20 // 2)])
            stream.append((seg, frac))
            truth["csi"].append(H)
            truth["act"].append(act)
            truth["t"].append(t)
            truth["start20"].append(pos + gap20 // 2)
            truth["rate"].append(rate)
            truth["length"].append(length)
            pos += len(seg)
            t += 1.0 / pkt_rate
    x20 = np.concatenate([s for s, _ in stream])
    n20 = np.arange(len(x20))
    x20 = x20 * np.exp(2j * np.pi * cfo_hz * n20 / FS20)
    x80 = interp4(x20)
    n80 = np.arange(len(x80))
    x80 = x80 * np.exp(2j * np.pi * f_off * n80 / FS80)
    sp = np.mean(np.abs(x20) ** 2)
    nz = np.sqrt(sp / 10 ** (snr_db / 10) / 2) * (rng.standard_normal(len(x80)) + 1j * rng.standard_normal(len(x80)))
    x80 = (x80 + nz * 2.0)                                  # noise over the 80 MHz band (4x)
    x80 *= amp / np.sqrt(np.mean(np.abs(x80) ** 2))
    iq = np.stack([np.clip(np.round(x80.real), -32768, 32767),
                   np.clip(np.round(x80.imag), -32768, 32767)], 1).astype(np.int16)
    for k in ("csi",):
        truth[k] = np.array(truth[k])
    truth.update(pkt_rate=pkt_rate, snr_db=snr_db, cfo_hz=cfo_hz, f_off=f_off, fc=fc)
    return iq, truth


def pack_iq_words(iq):
    """int16 [N,2] -> uint32 words {Q[31:16], I[15:0]} (the AXIS sample format)."""
    i = iq[:, 0].astype(np.int64) & 0xFFFF
    q = iq[:, 1].astype(np.int64) & 0xFFFF
    return ((q << 16) | i).astype(np.uint32)


def nco_inc_for(f_off_hz, fs=FS80):
    """wifi_ddc NCO increment that moves a tone at +f_off_hz to DC."""
    return int(round(-f_off_hz / fs * 2 ** 32)) & 0xFFFFFFFF


# ---- HLS constant tables (hls/common/wifi_tables.h) ---------------------------
def write_hls_tables(path):
    """Emit every constant the PL needs, from the SAME definitions as this model,
    so the HLS C++ and the golden model cannot drift."""
    tr, ti = ltf_sign_ref()
    L = ltf_freq()
    perm = interleave_perm()
    cos1k = np.round(np.cos(2 * np.pi * np.arange(1024) / 1024) * 32767).astype(int)
    cos4k = np.cos(2 * np.pi * np.arange(4096) / 4096).astype(np.float32)
    tw = np.exp(-2j * np.pi * np.arange(32) / 64)

    def arr(name, typ, vals, fmt="{}", per=16):
        s = f"static const {typ} {name}[{len(vals)}] = {{\n"
        fm = fmt if callable(fmt) else fmt.format
        rows = [", ".join(fm(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
        return s + ",\n".join("    " + r for r in rows) + "};\n"

    f32 = lambda v: f"{float(v):.9e}f"
    with open(path, "w") as f:
        f.write("#pragma once\n// Generated by csi_ref/wifi_ofdm.py write_hls_tables() - do not edit.\n\n")
        f.write("// sign-quantised L-LTF period T[i]: +1 -> 0, -1 -> 1 (bit set = negate)\n")
        f.write(arr("LTF_NEG_RE", "unsigned char", [int(v < 0) for v in tr]))
        f.write(arr("LTF_NEG_IM", "unsigned char", [int(v < 0) for v in ti]))
        f.write("// L-LTF on FFT bins 0..63: +1/-1, 0 = null\n")
        f.write(arr("LTF_BIN", "signed char", [int(v) for v in L]))
        f.write("// 48 L-SIG data subcarrier bins in increasing frequency; pilots; pilot values (p0=+1)\n")
        f.write(arr("DATA_BIN", "unsigned char", [int(k) % 64 for k in DATA_K]))
        f.write(arr("PILOT_BIN", "unsigned char", [int(k) % 64 for k in PILOT_K]))
        f.write(arr("PILOT_VAL", "signed char", [int(v) for v in PILOT_V]))
        f.write("// BPSK interleaver: coded bit k sits on data subcarrier INTLV[k]\n")
        f.write(arr("INTLV", "unsigned char", [int(v) for v in perm]))
        f.write("// Q1.15 cosine, 1024 points (wifi_ddc NCO)\n")
        f.write(arr("COS1K", "short", [int(v) for v in cos1k]))
        f.write("// float cosine, 4096 points (ofdm_be / csi_feat phase rotators)\n")
        f.write(arr("COS4K", "float", cos4k, f32, 8))
        f.write("// FFT-64 twiddles W^k = exp(-j 2 pi k / 64), k = 0..31\n")
        f.write(arr("TW_RE", "float", tw.real, f32, 8))
        f.write(arr("TW_IM", "float", tw.imag, f32, 8))
