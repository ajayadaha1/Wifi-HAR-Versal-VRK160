// ---------------------------------------------------------------------------
// ofdm_be.cpp - 802.11 OFDM receiver back end (one detected packet per call).
//
//   capture (N_CAP samples + trailer) from ofdm_fe
//   -> coarse CFO  w_c = angle(C)/16                         (L-STF)
//   -> fine CFO    w   = w_c + wrap(angle(R) - 64 w_c)/64,   R = sum y[T1+64+i] conj(y[T1+i])
//   -> de-rotate T1 / T2 / L-SIG windows (FFT start advanced `adv` samples into the CP)
//   -> 64-pt FFTs;  H[k] = (Y1[k]+Y2[k])/2 * L[k] / 64     (the CSI)
//   -> noise from Y1-Y2, SNR
//   -> L-SIG: Z = Ys conj(64 H), pilot CPE, BPSK soft bits, de-interleave,
//      6-bit soft K=7 r=1/2 Viterbi -> RATE/LENGTH/parity/tail check
//   -> one CSI record (REC_WORDS) to csi_feat, optionally a copy to DDR (rec).
// Mirrors csi_ref/wifi_ofdm.py _rx_capture().
//
// Registers (PL_OFDM_BE_BASE):
//   ctrl : [0] rec_en (copy records to the s2mm_csi drain; must be drained!)
//          [1] rec_sig_only (copy only L-SIG-valid packets)  [7:4] adv (0 -> 2)
//   epoch: change -> reset the packet counters (they also reset in-band when the
//          front-end capture sequence restarts at 0). Captures flagged CAP_STALE
//          (completed by ofdm_fe while armed for a restart) are read and dropped.
//   status: n_pkt, n_sig_ok, last_sig, n_resync
// ---------------------------------------------------------------------------
#include "../common/wifi_common.h"
#include "../common/wifi_tables.h"
#include "../common/fmath.h"
#include <math.h>

static const unsigned char RATE_MBPS[8] = {48, 24, 12, 6, 54, 36, 18, 9};  // L-SIG code 8..15

static inline int parity7(int v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return v & 1;
}

static void fft64(float xr[64], float xi[64]) {
    float tr[64], ti[64];
bitrev:
    for (int i = 0; i < 64; i++) {
#pragma HLS PIPELINE II=1
        int r = ((i & 1) << 5) | ((i & 2) << 3) | ((i & 4) << 1) | ((i & 8) >> 1) | ((i & 16) >> 3) | ((i & 32) >> 5);
        tr[r] = xr[i];
        ti[r] = xi[i];
    }
stages:
    for (int st = 0; st < 6; st++) {
        const int half = 1 << st;
        const int step = 32 >> st;
    bfly:
        for (int b = 0; b < 32; b++) {
#pragma HLS PIPELINE II=2
#pragma HLS DEPENDENCE variable=tr inter false
#pragma HLS DEPENDENCE variable=ti inter false
            const int grp = b >> st;
            const int j = b & (half - 1);
            const int i0 = (grp << (st + 1)) + j;
            const int i1 = i0 + half;
            const float wr = TW_RE[j * step], wi = TW_IM[j * step];
            const float ur = tr[i0], ui = ti[i0];
            const float vr = tr[i1] * wr - ti[i1] * wi;
            const float vi = tr[i1] * wi + ti[i1] * wr;
            tr[i0] = ur + vr;
            ti[i0] = ui + vi;
            tr[i1] = ur - vr;
            ti[i1] = ui - vi;
        }
    }
copyback:
    for (int i = 0; i < 64; i++) {
#pragma HLS PIPELINE II=1
        xr[i] = tr[i];
        xi[i] = ti[i];
    }
}

static ap_uint<24> viterbi24(const ap_int<8> q[48]) {
    ap_int<16> pm[64];
#pragma HLS ARRAY_PARTITION variable=pm complete
    ap_uint<64> dec[24];
init:
    for (int s = 0; s < 64; s++) {
#pragma HLS UNROLL
        pm[s] = (s == 0) ? (ap_int<16>)0 : (ap_int<16>)-16384;
    }
steps:
    for (int t = 0; t < 24; t++) {
#pragma HLS PIPELINE II=2
        const ap_int<16> sa = q[2 * t], sb = q[2 * t + 1];
        ap_int<16> npm[64];
#pragma HLS ARRAY_PARTITION variable=npm complete
        ap_uint<64> d = 0;
        for (int ns = 0; ns < 64; ns++) {
#pragma HLS UNROLL
            const int b = ns >> 5;
            const int s0 = (ns << 1) & 63, s1 = s0 | 1;
            const int st0 = (b << 6) | s0, st1 = (b << 6) | s1;
            const ap_int<16> m0 = pm[s0] + (parity7(st0 & 0133) ? sa : (ap_int<16>)-sa) + (parity7(st0 & 0171) ? sb : (ap_int<16>)-sb);
            const ap_int<16> m1 = pm[s1] + (parity7(st1 & 0133) ? sa : (ap_int<16>)-sa) + (parity7(st1 & 0171) ? sb : (ap_int<16>)-sb);
            if (m1 > m0) { npm[ns] = m1; d[ns] = 1; }
            else { npm[ns] = m0; d[ns] = 0; }
        }
        for (int ns = 0; ns < 64; ns++) {
#pragma HLS UNROLL
            pm[ns] = npm[ns];
        }
        dec[t] = d;
    }
    ap_uint<24> bits = 0;
    int s = 0;
tb:
    for (int t = 23; t >= 0; t--) {
#pragma HLS PIPELINE II=1
        bits[t] = (s >> 5) & 1;
        s = ((s << 1) & 63) | (int)dec[t][s];
    }
    return bits;
}

void ofdm_be(hls::stream<axis32_t>& cap, hls::stream<axis32_t>& csi, hls::stream<axis32_t>& rec,
             ap_uint<32> ctrl, ap_uint<32> epoch, ap_uint<32>* n_pkt, ap_uint<32>* n_sig_ok, ap_uint<32>* last_sig,
             ap_uint<32>* n_resync) {
#pragma HLS INTERFACE mode=axis port=cap
#pragma HLS INTERFACE mode=axis port=csi
#pragma HLS INTERFACE mode=axis port=rec
#pragma HLS INTERFACE mode=s_axilite port=ctrl bundle=control
#pragma HLS INTERFACE mode=s_axilite port=epoch bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_pkt bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_sig_ok bundle=control
#pragma HLS INTERFACE mode=s_axilite port=last_sig bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_resync bundle=control
#ifdef HLS_COSIM_HS
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control
#else
#pragma HLS INTERFACE mode=ap_ctrl_none port=return
#endif
    static ap_uint<32> seq = 0, nok = 0, nres = 0;
    static ap_uint<32> last_epoch = 0;

    float yr[N_CAP], yi[N_CAP];
    uint32_t trl[CAP_TRL];
    bool framing_ok = true, last_at_end = false;
rd_samples:
    for (int n = 0; n < N_CAP; n++) {
#pragma HLS PIPELINE II=1
        axis32_t w = cap.read();
        yr[n] = (float)(ap_int<16>)w.data(15, 0);
        yi[n] = (float)(ap_int<16>)w.data(31, 16);
        if (w.last) framing_ok = false;
    }
rd_trailer:
    for (int k = 0; k < CAP_TRL; k++) {
#pragma HLS PIPELINE II=1
        axis32_t w = cap.read();
        trl[k] = (uint32_t)w.data;
        if (k == CAP_TRL - 1) last_at_end = (w.last == 1);
        else if (w.last) framing_ok = false;
    }
    if (!framing_ok || !last_at_end || trl[0] != CAP_MAGIC) {
        // lost capture alignment: drop words up to the next TLAST, emit nothing
        nres++;
        *n_resync = nres;
        bool l = last_at_end;
    resync:
        for (int g = 0; g < 4 * CAP_WORDS; g++) {
            if (l) break;
            axis32_t w = cap.read();
            l = (w.last == 1);
        }
        return;
    }

    if (trl[7] & CAP_STALE) return;         // previous run's capture: no record, no count

    // Restart in-band: the first capture after an ofdm_fe restart has seq 0 (or the
    // host changed `epoch`). Evaluated after the capture is read, so a call already
    // blocked on the stream when the register was written cannot miss it.
    if (trl[7] == 0 || epoch != last_epoch) { last_epoch = epoch; seq = 0; nok = 0; nres = 0; }
    int t1 = (int)trl[1];
    if (t1 < T1_LO) t1 = T1_LO;
    if (t1 > T1_HI) t1 = T1_HI;
    const float Cr = u2f(trl[2]), Ci = u2f(trl[3]), P = u2f(trl[4]);
    ap_uint<64> ts = ((ap_uint<64>)trl[6] << 32) | (ap_uint<64>)trl[5];
    ts += (ap_uint<64>)t1;
    const int adv = (ctrl(7, 4) == 0) ? 2 : (int)ctrl(7, 4);

    // ---- CFO -----------------------------------------------------------------
    const float wc = f_atan2(Ci, Cr) / (float)STF_DLY;
    float pr[8], pi[8];
#pragma HLS ARRAY_PARTITION variable=pr complete
#pragma HLS ARRAY_PARTITION variable=pi complete
    for (int j = 0; j < 8; j++) {
#pragma HLS UNROLL
        pr[j] = 0.0f;
        pi[j] = 0.0f;
    }
fine_cfo:
    for (int i = 0; i < 64; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=pr inter distance=8 true
#pragma HLS DEPENDENCE variable=pi inter distance=8 true
        const float a = yr[t1 + 64 + i], b = yi[t1 + 64 + i];
        const float c = yr[t1 + i], d = yi[t1 + i];
        pr[i & 7] += a * c + b * d;
        pi[i & 7] += b * c - a * d;
    }
    const float Rr = ((pr[0] + pr[1]) + (pr[2] + pr[3])) + ((pr[4] + pr[5]) + (pr[6] + pr[7]));
    const float Ri = ((pi[0] + pi[1]) + (pi[2] + pi[3])) + ((pi[4] + pi[5]) + (pi[6] + pi[7]));
    const float pred = 64.0f * wc;
    const float w = (pred + f_wrap(f_atan2(Ri, Rr) - pred)) / 64.0f;

    // ---- de-rotate + split T1 / T2 / SIG windows -------------------------------
    float ar[64], ai[64], br[64], bi[64], sr[64], si[64];
    const int s1 = t1 - adv;
    const int pinc = f_rad2ph(-w);
derot:
    for (int i = 0; i < 208; i++) {
#pragma HLS PIPELINE II=1
        const int n = s1 + i;
        float c, s;
        f_cis((unsigned int)(pinc * (n - t1)), c, s);
        const float zr = yr[n] * c - yi[n] * s;
        const float zi = yr[n] * s + yi[n] * c;
        if (i < 64) { ar[i] = zr; ai[i] = zi; }
        else if (i < 128) { br[i - 64] = zr; bi[i - 64] = zi; }
        else if (i >= 144) { sr[i - 144] = zr; si[i - 144] = zi; }
    }
    fft64(ar, ai);
    fft64(br, bi);
    fft64(sr, si);

    // ---- channel estimate + SNR ----------------------------------------------
    float hr[64], hi[64];
    float nacc = 0.0f, sacc = 0.0f;
chest:
    for (int k = 0; k < 64; k++) {
#pragma HLS PIPELINE
        const int L = LTF_BIN[k];
        float xr = 0.0f, xi = 0.0f;
        if (L != 0) {
            xr = 0.5f * (ar[k] + br[k]) * (float)L * (1.0f / 64.0f);
            xi = 0.5f * (ai[k] + bi[k]) * (float)L * (1.0f / 64.0f);
            const float dr = ar[k] - br[k], di = ai[k] - bi[k];
            nacc += dr * dr + di * di;
            sacc += xr * xr + xi * xi;
        }
        hr[k] = xr;
        hi[k] = xi;
    }
    const float noise = nacc / (2.0f * 52.0f * 4096.0f);
    const float sigp = sacc / 52.0f;
    const float snr = (noise > 0.0f) ? sigp / noise : 1.0e9f;

    // ---- L-SIG ----------------------------------------------------------------
    float cpr = 0.0f, cpi = 0.0f;
pilots:
    for (int p = 0; p < 4; p++) {
#pragma HLS PIPELINE
        const int k = PILOT_BIN[p];
        const float h64r = 64.0f * hr[k], h64i = 64.0f * hi[k];
        const float zr = sr[k] * h64r + si[k] * h64i;
        const float zi = si[k] * h64r - sr[k] * h64i;
        cpr += zr * (float)PILOT_VAL[p];
        cpi += zi * (float)PILOT_VAL[p];
    }
    const float cpe = f_atan2(cpi, cpr);
    float cc, ss;
    f_cis((unsigned int)f_rad2ph(-cpe), cc, ss);
    float soft_sc[48];
    uint32_t umx = 0;          // max |v| as IEEE bits (monotonic for v >= 0): an integer
                               // compare keeps the 1-cycle recurrence off the fcmp
data_sc:
    for (int i = 0; i < 48; i++) {
#pragma HLS PIPELINE II=1
        const int k = DATA_BIN[i];
        const float h64r = 64.0f * hr[k], h64i = 64.0f * hi[k];
        const float zr = sr[k] * h64r + si[k] * h64i;
        const float zi = si[k] * h64r - sr[k] * h64i;
        const float v = zr * cc - zi * ss;
        soft_sc[i] = v;
        const uint32_t ua = f2u(v) & 0x7FFFFFFFu;
        if (ua > umx) umx = ua;
    }
    const float mx = u2f(umx);
    ap_int<8> q[48];
    const float qs = (mx > 0.0f) ? 31.0f / mx : 0.0f;
quant:
    for (int k = 0; k < 48; k++) {
#pragma HLS PIPELINE II=1
        const float v = soft_sc[INTLV[k]] * qs;
        int iv = (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
        if (iv > 31) iv = 31;
        if (iv < -31) iv = -31;
        q[k] = (ap_int<8>)iv;
    }
    const ap_uint<24> sb = viterbi24(q);
    const int rate_code = (int)sb(3, 0);
    const int length = (int)sb(16, 5);
    int par = 0;
    for (int b = 0; b < 18; b++) {
#pragma HLS UNROLL
        par ^= (int)sb[b];
    }
    const bool parity_ok = (par == 0);
    const bool tail_ok = (sb(23, 18) == 0);
    const bool sig_ok = parity_ok && tail_ok && (sb[4] == 0) && (sb[3] == 1) && (length > 0);
    const int mbps = (sb[3] == 1) ? (int)RATE_MBPS[rate_code & 7] : 0;

    // ---- record ---------------------------------------------------------------
    uint32_t hdr[REC_HDR];
#pragma HLS ARRAY_PARTITION variable=hdr complete
    hdr[RW_MAGIC] = REC_MAGIC;
    hdr[RW_SEQ] = (uint32_t)seq;
    hdr[RW_TS_LO] = (uint32_t)ts(31, 0);
    hdr[RW_TS_HI] = (uint32_t)ts(63, 32);
    hdr[RW_SIG] = (uint32_t)sb | ((uint32_t)sig_ok << 24) | ((uint32_t)parity_ok << 25) | ((uint32_t)tail_ok << 26);
    hdr[RW_CFO] = f2u(w * (20.0e6f / F_2PI));
    hdr[RW_RSSI] = f2u(P * (1.0f / (float)STF_WIN));
    hdr[RW_SNR] = f2u(snr);
    hdr[RW_T1] = (uint32_t)t1;
    hdr[RW_METRIC] = f2u((P > 0.0f) ? sqrtf(Cr * Cr + Ci * Ci) / P : 0.0f);
    hdr[RW_NOISE] = f2u(noise);
    hdr[RW_SIGPWR] = f2u(sigp);
    hdr[RW_FMT] = 64u | ((uint32_t)mbps << 16);
    hdr[RW_LEN] = (uint32_t)length;
    hdr[RW_RSV0] = trl[7];
    hdr[RW_RSV1] = 0;

    const bool do_rec = ctrl[0] && (!ctrl[1] || sig_ok);
emit:
    for (int i = 0; i < REC_WORDS; i++) {
#pragma HLS PIPELINE II=1
        uint32_t v;
        if (i < REC_HDR) v = hdr[i];
        else {
            const int j = i - REC_HDR;
            v = (j & 1) ? f2u(hi[j >> 1]) : f2u(hr[j >> 1]);
        }
        const axis32_t o = mkword(v, i == REC_WORDS - 1);
        csi.write(o);
        if (do_rec) rec.write(o);
    }
    seq++;
    if (sig_ok) nok++;
    *n_pkt = seq;
    *n_sig_ok = nok;
    *last_sig = hdr[RW_SIG];
    *n_resync = nres;
}
