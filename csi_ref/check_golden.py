#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# check_golden.py - independent cross-check of the bit-exact C++ golden model
# (hls/tb/golden_chain, i.e. the HLS sources) against the Python PHY model.
#
#   1. detection : every transmitted packet found once (no misses / duplicates)
#   2. L-SIG     : decoded RATE / LENGTH == transmitted, sig_ok set
#   3. CSI       : |H_hw| vs ground truth |H| (common gain removed) and vs the
#                  Python float receiver run on the SAME 20 MSPS samples (s20.bin)
#   4. CFO       : estimate vs the injected CFO
#   5. features  : AIE motion power separates the activity segments (demo preset)
# Exit 0 = PASS. Usage: check_golden.py <golden_outdir>
# ---------------------------------------------------------------------------
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wifi_ofdm as W  # noqa: E402

REC_WORDS, REC_HDR, FEAT_WORDS, TAG_WORDS = 144, 16, 52, 12


def load_records(path, words):
    raw = np.fromfile(path, dtype=np.uint32)
    n = raw.size // words
    return raw[: n * words].reshape(n, words)


def rec_csi(r):
    f = r[REC_HDR:].view(np.float32)
    return f[0::2].astype(np.float64) + 1j * f[1::2]


def main(od):
    tr = np.load(os.path.join(od, "truth.npz"))
    recs = load_records(os.path.join(od, "csi_rec.bin"), REC_WORDS)
    n_tx = len(tr["act"])
    ok = True
    print(f"packets: transmitted={n_tx}  records={len(recs)}")
    sig = recs[:, 4]
    sig_ok = (sig >> 24) & 1
    good = recs[sig_ok == 1]
    print(f"L-SIG valid: {int(sig_ok.sum())}/{len(recs)}")
    if len(good) < 0.995 * n_tx:
        print(f"FAIL: expected >= 99.5% of {n_tx} packets L-SIG valid, got {len(good)}")
        ok = False
    U = W.USED % 64
    # match records to transmitted packets by time: T1 ~ start20 + 192 + filter delay
    ts = good[:, 2].astype(np.int64) | (good[:, 3].astype(np.int64) << 32)
    exp = tr["start20"].astype(np.int64) + 192
    idx = np.array([int(np.argmin(np.abs(exp - t))) for t in ts])
    dly = ts - exp[idx]
    uniq = len(set(idx.tolist()))
    print(f"matched {uniq}/{n_tx} packets (T1 offset median {int(np.median(dly))} samples, "
          f"spread {int(dly.max() - dly.min())})")
    if uniq < 0.995 * n_tx or uniq != len(idx):
        ok = False
    rate_ok = np.mean(((good[:, 12] >> 16) & 0xFF) == tr["rate"][idx])
    len_ok = np.mean(good[:, 13] == tr["length"][idx])
    print(f"RATE match {rate_ok * 100:.1f}%  LENGTH match {len_ok * 100:.1f}%")
    if rate_ok < 1.0 or len_ok < 1.0:
        ok = False
    n = len(good)
    cfo = good[:n, 5].view(np.float32)
    print(f"CFO est: mean {cfo.mean():.0f} Hz  std {cfo.std():.0f} Hz  (injected {float(tr['cfo_hz']):.0f})")
    if abs(cfo.mean() - float(tr["cfo_hz"])) > 2000:
        ok = False
    # amplitude-only comparison (gain free; phase slopes from timing are expected)
    errs = []
    for i in range(n):
        h = np.abs(rec_csi(good[i])[U])
        t = np.abs(tr["csi"][idx[i]][U])
        g = np.dot(h, t) / np.dot(t, t)
        errs.append(np.linalg.norm(h - g * t) / np.linalg.norm(g * t))
    errs = np.array(errs)
    print(f"|CSI| vs truth: median rel err {np.median(errs):.3f}  95% {np.percentile(errs, 95):.3f}")
    if np.median(errs) > 0.08:
        ok = False
    snr = 10 * np.log10(good[:n, 7].view(np.float32))
    print(f"SNR est: median {np.median(snr):.1f} dB (injected {float(tr['snr_db']):.1f} dB)")
    # the Python float receiver on the same 20 MSPS stream
    s20 = np.fromfile(os.path.join(od, "s20.bin"), dtype=np.uint32)
    y = ((s20 & 0xFFFF).astype(np.int16).astype(float) + 1j * (s20 >> 16).astype(np.uint16).astype(np.int16))
    ref = W.rx_reference(y)
    ref = [r for r in ref if r["sig"]["ok"]]
    m = min(len(ref), n)
    d = []
    for i in range(m):
        a = rec_csi(good[i])[U]
        b = ref[i]["csi"][U]
        d.append(np.linalg.norm(a - b) / np.linalg.norm(b))
    d = np.array(d) if d else np.array([1.0])
    t1_match = np.mean([int(good[i, 8]) == ref[i]["t1"] for i in range(m)]) if m else 0.0
    print(f"python RX: {len(ref)} valid packets; T1 match {t1_match * 100:.1f}%; "
          f"CSI (complex) hw-vs-python median rel diff {np.median(d):.2e} max {d.max():.2e}")
    if len(ref) != len(good) or np.median(d) > 1e-2:
        ok = False
    fpath = os.path.join(od, "feat.bin")
    if os.path.exists(fpath) and os.path.getsize(fpath) >= FEAT_WORDS * 4:
        feats = load_records(fpath, FEAT_WORDS)
        f = feats.view(np.float32)
        mot_pow = f[:, TAG_WORDS + 2]
        phs_var = f[:, TAG_WORDS + 3 + 33 + 1]
        seq = feats[:, 1]
        # each window ends at sample 255 + seq*hop -> packet index -> activity
        hop = 10
        end_pk = 255 + seq.astype(int) * hop
        acts = tr["act"]
        print(f"feature records: {len(feats)}")
        by = {}
        for e, p, v in zip(end_pk, mot_pow, phs_var):
            if e < len(acts):
                # only windows that are >= 90% one activity are labelled
                win = list(acts[max(0, e - 255): e + 1])
                lab = max(set(win), key=win.count)
                if win.count(lab) >= 0.9 * len(win):
                    by.setdefault(lab, []).append((p, v))
        for lab, vals in by.items():
            vals = np.array(vals)
            print(f"  {lab:10s} windows={len(vals):3d}  motion_power median {np.median(vals[:, 0]):.3e}  "
                  f"phase_var median {np.median(vals[:, 1]):.3e}")
        if "walking" in by and "empty" in by:
            sep = np.median(np.array(by["walking"])[:, 0]) / max(np.median(np.array(by["empty"])[:, 0]), 1e-12)
            print(f"  walking/empty motion-power ratio {sep:.1f}x")
            if sep < 10:
                ok = False
        if "breathing" in by and "empty" in by:
            sep = np.median(np.array(by["breathing"])[:, 1]) / max(np.median(np.array(by["empty"])[:, 1]), 1e-12)
            print(f"  breathing/empty phase-var ratio {sep:.1f}x")
    print("CHECK_GOLDEN", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
