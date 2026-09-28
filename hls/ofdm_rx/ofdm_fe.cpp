// ---------------------------------------------------------------------------
// ofdm_fe.cpp - 802.11 OFDM receiver front end (sample rate, II=1).
//
// Per 20 MSPS sample x[n]:
//   * L-STF detector (Schmidl-Cox): C = sum_{48} x[n]conj(x[n-16]),
//     P = sum_{48} |x[n]|^2, Pd = sum_{48} |x[n-16]|^2; "high" when
//     |C|^2 >= thr2*P*Pd (a correlation coefficient, <= 1 by Cauchy-Schwarz, so
//     packet tails running into noise cannot fake a plateau) and P > p_min;
//     trigger after n_plat consecutive highs (then hold-off).
//   * L-LTF timing: CFO-robust segmented sign correlator
//     M(p) = sum_{s<4} |sum_{i<16} x[p+16s+i] * conj(Tq[16s+i])|^2 (integer),
//     M2(p) = M(p) + M(p+64) peaks at T1 (T2 = T1+64).
//   * On trigger: stream a capture of N_CAP samples starting CAP_PRE before the
//     trigger, then an 8-word trailer {magic, T1 (argmax M2 over [T1_LO,T1_HI]),
//     C.re, C.im, P, ts_lo, ts_hi, seq} to ofdm_be.
// Mirrors csi_ref/wifi_ofdm.py rx_reference() (integer parts bit-exactly).
//
// Registers (PL_OFDM_FE_BASE): thr2 (f32, 0->0.49), p_min (f32), n_plat
// (0->24), holdoff (0->480, >= CAP_WORDS enforced), epoch (change -> arm: no new
// triggers; a capture in flight still completes - frame alignment towards ofdm_be is
// never broken - but its trailer seq word gets bit 31 (CAP_STALE) so ofdm_be drops it;
// full reset at the first non-zero input sample once idle, so the restart point is
// data-aligned. The host zero pre-roll, PREROLL_WORDS >= 4*CAP_WORDS, guarantees the
// stale capture ends before the new data arrives); status n_trig, p_now.
// ---------------------------------------------------------------------------
#include "../common/wifi_common.h"
#include "../common/wifi_tables.h"

void ofdm_fe(hls::stream<axis32_t>& iq_in, hls::stream<axis32_t>& cap,
             float thr2, float p_min, ap_uint<32> n_plat, ap_uint<32> holdoff,
             ap_uint<32> epoch, ap_uint<32>* n_trig, ap_uint<32>* p_now) {
#pragma HLS INTERFACE mode=axis port=iq_in
#pragma HLS INTERFACE mode=axis port=cap
#pragma HLS INTERFACE mode=s_axilite port=thr2 bundle=control
#pragma HLS INTERFACE mode=s_axilite port=p_min bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_plat bundle=control
#pragma HLS INTERFACE mode=s_axilite port=holdoff bundle=control
#pragma HLS INTERFACE mode=s_axilite port=epoch bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_trig bundle=control
#pragma HLS INTERFACE mode=s_axilite port=p_now bundle=control
#pragma HLS INTERFACE mode=ap_ctrl_none port=return
#pragma HLS PIPELINE II=1
    static ap_int<16> xr[65], xi[65];          // x[n-k] at index k
#pragma HLS ARRAY_PARTITION variable=xr complete
#pragma HLS ARRAY_PARTITION variable=xi complete
    static ap_uint<46> mh[65];                 // M(p-k) at index k
#pragma HLS ARRAY_PARTITION variable=mh complete
    static ap_int<40> cr = 0, ci = 0, pw = 0, pd = 0;
    static ap_uint<16> cnt = 0;
    static ap_uint<16> hold = 0;
    static bool capturing = false;
    static ap_uint<10> cidx = 0;
    static ap_uint<46> best = 0;
    static ap_uint<9> best_pos = T1_LO;
    static ap_uint<64> ts = 0;
    static ap_uint<64> cap_ts = 0;
    static float tcr = 0, tci = 0, tp = 0;
    static ap_uint<32> seq = 0;
    static ap_uint<32> last_epoch = 0;
    static bool armed = false;

    const float th = (thr2 > 0.0f) ? thr2 : 0.49f;
    const ap_uint<16> np = (n_plat == 0) ? (ap_uint<16>)24 : (ap_uint<16>)n_plat;
    ap_uint<16> ho = (holdoff == 0) ? (ap_uint<16>)480 : (ap_uint<16>)holdoff;
    if (ho < CAP_WORDS) ho = CAP_WORDS;

    // Deterministic restart (self-test): `epoch` only arms; the control state restarts
    // at the first non-zero input once no capture is in flight (wifi_ddc outputs zeros
    // while it is armed itself). The detector state needs no reset: the delay lines and
    // the running sums are exact integer recurrences, so the zero pre-roll (>= 130
    // samples) has already flushed them to exactly 0 - and keeping them out of the
    // restart keeps the float metric off the 1-cycle control recurrence (312.5 MHz).
    if (epoch != last_epoch) { last_epoch = epoch; armed = true; }
    axis32_t w = iq_in.read();
    if (armed && w.data != 0 && !capturing) {
        armed = false;
        cnt = 0; hold = 0; ts = 0; seq = 0;
    }
    for (int k = 64; k > 0; k--) { xr[k] = xr[k - 1]; xi[k] = xi[k - 1]; }
    xr[0] = (ap_int<16>)w.data(15, 0);
    xi[0] = (ap_int<16>)w.data(31, 16);

    // ---- L-STF delay-16 autocorrelation + power (running 48-sample sums) ----
    ap_int<34> nr = (ap_int<34>)xr[0] * xr[16] + (ap_int<34>)xi[0] * xi[16];
    ap_int<34> nim = (ap_int<34>)xi[0] * xr[16] - (ap_int<34>)xr[0] * xi[16];
    ap_int<34> orr = (ap_int<34>)xr[48] * xr[64] + (ap_int<34>)xi[48] * xi[64];
    ap_int<34> oim = (ap_int<34>)xi[48] * xr[64] - (ap_int<34>)xr[48] * xi[64];
    ap_int<34> pn = (ap_int<34>)xr[0] * xr[0] + (ap_int<34>)xi[0] * xi[0];
    ap_int<34> po = (ap_int<34>)xr[48] * xr[48] + (ap_int<34>)xi[48] * xi[48];
    ap_int<34> dn = (ap_int<34>)xr[16] * xr[16] + (ap_int<34>)xi[16] * xi[16];
    ap_int<34> dq = (ap_int<34>)xr[64] * xr[64] + (ap_int<34>)xi[64] * xi[64];
    // Metric of the PREVIOUS sample from the registered sums: keeps the int->float
    // conversions off the 1-cycle accumulator recurrence (312.5 MHz). The capture
    // tap / T1 mapping below are shifted by one to compensate, so captures are
    // bit-identical to evaluating sample n at cycle n (csi_ref rx_reference()).
    const float fcr = (float)(long long)cr, fci = (float)(long long)ci, fp = (float)(long long)pw;
    const float fpd = (float)(long long)pd;
    const bool hi = (fcr * fcr + fci * fci >= th * (fp * fpd)) && (fp > p_min);
    cr += nr - orr;
    ci += nim - oim;
    pw += pn - po;
    pd += dn - dq;

    // ---- L-LTF segmented sign correlator at position p = n-63 ----------------
    ap_uint<46> m = 0;
    for (int s = 0; s < 4; s++) {
#pragma HLS UNROLL
        ap_int<22> sr = 0, si = 0;
        for (int i = 0; i < 16; i++) {
#pragma HLS UNROLL
            const int idx = 16 * s + i;
            ap_int<17> a = xr[63 - idx], b = xi[63 - idx];
            // x * conj(tr + j ti) = (a tr + b ti) + j (b tr - a ti)
            ap_int<17> ar = LTF_NEG_RE[idx] ? (ap_int<17>)(-a) : a;
            ap_int<17> bi = LTF_NEG_IM[idx] ? (ap_int<17>)(-b) : b;
            ap_int<17> br = LTF_NEG_RE[idx] ? (ap_int<17>)(-b) : b;
            ap_int<17> ai2 = LTF_NEG_IM[idx] ? (ap_int<17>)(-a) : a;
            sr += ar + bi;
            si += br - ai2;
        }
        m += (ap_uint<46>)((ap_int<44>)sr * sr + (ap_int<44>)si * si);
    }
    for (int k = 64; k > 0; k--) mh[k] = mh[k - 1];
    mh[0] = m;
    ap_uint<46> m2 = mh[64] + mh[0];            // M2 at position (n-63)-64

    // ---- trigger / capture state machine (flags from registered state) ------
    // capture word c is x[n-33] = sample (trigger-32)+c; M2 here is at position
    // n-127 = capture coordinate c-94.
    const bool idle = !capturing && (hold == 0) && !armed;
    const bool at_plat = (cnt == (ap_uint<16>)(np - 1));
    const bool trig = idle && hi && at_plat;
    if (trig) {
        cap_ts = ts - (CAP_PRE + 1);
        tcr = fcr; tci = fci; tp = fp;
    }
    cnt = (idle && hi && !trig) ? (ap_uint<16>)(cnt + 1) : (ap_uint<16>)0;
    hold = trig ? ho : (hold > 0 ? (ap_uint<16>)(hold - 1) : (ap_uint<16>)0);
    const bool active = capturing || trig;
    const ap_uint<10> c = trig ? (ap_uint<10>)0 : cidx;
    const ap_uint<46> best_in = trig ? (ap_uint<46>)0 : best;
    if (trig) best_pos = T1_LO;
    if (active && c >= T1_LO + 94 && c <= T1_HI + 94 && m2 > best_in) {
        best = m2;
        best_pos = c - 94;
    } else {
        best = best_in;
    }
    if (active) {
        ap_uint<32> d;
        bool lst = false;
        if (c < N_CAP) {
            d(15, 0) = (ap_uint<16>)xr[CAP_PRE + 1];
            d(31, 16) = (ap_uint<16>)xi[CAP_PRE + 1];
        } else {
            switch ((int)(c - N_CAP)) {
                case 0: d = CAP_MAGIC; break;
                case 1: d = (ap_uint<32>)best_pos; break;
                case 2: d = f2u(tcr); break;
                case 3: d = f2u(tci); break;
                case 4: d = f2u(tp); break;
                case 5: d = cap_ts(31, 0); break;
                case 6: d = cap_ts(63, 32); break;
                default: d = seq; d[31] = armed; lst = true; break;   // CAP_STALE
            }
        }
        cap.write(mkword(d, lst));
        capturing = (c != CAP_WORDS - 1);
        if (c == CAP_WORDS - 1) seq++;
        cidx = c + 1;
    }
    ts++;
    *n_trig = seq;
    *p_now = f2u(fp);
}
