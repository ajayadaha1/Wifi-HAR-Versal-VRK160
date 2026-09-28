#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# make_vectors.py - build IQ playback vectors (80 MSPS, the RF-DC DDC output
# format) for the HLS testbenches, the golden model and the on-board self-test.
#
#   --preset quick : 40 packets, mixed rates, +5 MHz channel offset (exercises
#                    the wifi_ddc NCO), SNR 25 dB, CFO 61 kHz  -> OFDM RX checks
#   --preset demo  : the activity scenario (empty / walking / breathing / fall /
#                    lying / walking, ~51 s @ 20 packets/s)     -> feature checks,
#                    dashboard demo and the board self-test
# Writes <out>/iq80.bin (uint32 {Q,I} words), truth.npz, golden_args.txt,
# vectors.json (metadata the host app prints).
# ---------------------------------------------------------------------------
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wifi_ofdm as W  # noqa: E402

PRESETS = {
    "quick": dict(segments=[("breathing", 2.0)], pkt_rate=20.0, snr_db=25.0, cfo_hz=61e3,
                  f_off=5e6, seed=11),
    "demo": dict(segments=W.SCENARIO_DEFAULT, pkt_rate=20.0, snr_db=30.0, cfo_hz=37e3,
                 f_off=0.0, seed=7),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--preset", choices=sorted(PRESETS), default="quick")
    ap.add_argument("--snr", type=float)
    ap.add_argument("--f-off", type=float)
    a = ap.parse_args()
    cfg = dict(PRESETS[a.preset])
    if a.snr is not None:
        cfg["snr_db"] = a.snr
    if a.f_off is not None:
        cfg["f_off"] = a.f_off
    os.makedirs(a.out, exist_ok=True)
    iq, truth = W.scenario(**cfg)
    words = W.pack_iq_words(iq)
    words.tofile(os.path.join(a.out, "iq80.bin"))
    np.savez_compressed(os.path.join(a.out, "truth.npz"),
                        csi=truth["csi"], act=np.array(truth["act"]), t=np.array(truth["t"]),
                        start20=np.array(truth["start20"]), rate=np.array(truth["rate"]),
                        length=np.array(truth["length"]), pkt_rate=truth["pkt_rate"],
                        snr_db=truth["snr_db"], cfo_hz=truth["cfo_hz"], f_off=truth["f_off"])
    # pacing cuts for `host_wifi play`: word offsets mid-gap before every 20th packet
    gap = 100                                                 # gap20 // 2 (scenario default)
    cuts = [0] + [int(max(0, s0 - gap // 2) * 4) for s0 in truth["start20"][20::20]]
    with open(os.path.join(a.out, "chunks.txt"), "w") as f:
        f.write("\n".join(str(c) for c in cuts) + "\n")
    nco = W.nco_inc_for(cfg["f_off"])
    with open(os.path.join(a.out, "golden_args.txt"), "w") as f:
        f.write(f"--nco {nco} --pkt-bins 1 --sig-only 1 --fs {cfg['pkt_rate']}\n")
    meta = dict(preset=a.preset, n_words=int(words.size), n_packets=len(truth["act"]),
                pkt_rate=cfg["pkt_rate"], snr_db=cfg["snr_db"], cfo_hz=cfg["cfo_hz"],
                f_off_hz=cfg["f_off"], nco_inc=nco,
                segments=[[s, d] for s, d in cfg["segments"]])
    with open(os.path.join(a.out, "vectors.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print(f"make_vectors: {a.preset}: {len(truth['act'])} packets, {words.size} words "
          f"({words.nbytes / 1e6:.1f} MB) -> {a.out}  nco_inc={nco}")


if __name__ == "__main__":
    main()
