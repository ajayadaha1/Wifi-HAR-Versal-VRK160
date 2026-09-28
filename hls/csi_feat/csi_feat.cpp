// ---------------------------------------------------------------------------
// csi_feat.cpp - per-packet CSI -> the three AI Engine branch inputs.
//
// For every CSI record from ofdm_be (optionally only L-SIG-valid ones):
//   amp    = mean_k |H[k]| over the 52 used subcarriers
//   a_rel  = amp / EMA(amp) - 1              (gain-invariant, zero-mean: the AIE FIR
//            is causal with zero-padded history, so a ~1.0 input would put a start-
//            of-window step transient into the motion power)
//   phase  : linear sanitisation across subcarriers (removes CFO / timing):
//            slope a = angle(sum H[k+1] conj H[k]), offset b = angle(sum H[k] e^{-j a k}),
//            phi = angle(H[k_ref] e^{-j (a k_ref + b)})
// Samples are formed per packet (ctrl[0]=1, playback) or per time bin of
// bin_us (ctrl[0]=0, live RF: bins are averaged, empty bins hold the last value).
// Every `hop` samples (after 256 samples of history) three windows go to the AIE:
//   mot[256] = a_rel @ fs_csi           -> FIR band-pass -> stats   (motion)
//   brt[64]  = a_rel averaged by br_dec  -> windowed DFT |X|        (breathing)
//   phs[256] = phi    @ fs_csi          -> stats                    (phase-var)
// plus a TAG_WORDS tag (window seq/time/packet count/RSSI/SNR/fs/checksums) to
// feat_pack; tag[4] bit 31 = breathing history full (>= 64*br_dec samples). Registers (PL_CSI_FEAT_BASE): ctrl [0] packet bins [1] sig_ok only;
// bin_us (0->50000), hop (0->10), br_dec (0->10), k_ref (0->13), fs_nom (0->20),
// alpha (EMA gain tracker, 0->1/256), epoch (change -> clear all history).
// Status n_in/n_win/n_samp.
// ---------------------------------------------------------------------------
#include "../common/wifi_common.h"
#include "../common/wifi_tables.h"
#include "../common/fmath.h"
#include <math.h>

struct FeatState {
    float mh[WIN_MOT], ph[WIN_PHS], bh[WIN_BRT];
    int wp, bwp;
    float ema;
    bool have_ema;
    uint32_t nsamp, since_win, win_seq;
    float br_acc;
    int br_n;
    bool have_bin;
    ap_uint<64> cur_bin;
    float acc_a, acc_p;
    int acc_n;
    float last_a, last_p;
    uint32_t w_pk;
    float w_rssi, w_snr, w_cfo;
    ap_uint<64> last_ts;
};

static void emit_windows(FeatState& st, float fs, bool brt_full, hls::stream<axis32_t>& mot,
                         hls::stream<axis32_t>& brt, hls::stream<axis32_t>& phs, hls::stream<axis32_t>& tag) {
    // Window conditioning (the AIE kernels are the fixed VCK190 cores):
    //   mot : minus its mean  - the AIE FIR is causal with zero history, so an offset
    //         would put a start-of-window step transient into the motion power
    //   brt : minus mean and linear trend - EMA gain-tracking drift must not leak
    //         into the 0.1-0.6 Hz breathing bins
    //   phs : centred on its circular mean - no +/-pi wrap in the variance
    float mm = 0.0f, bm = 0.0f, bt = 0.0f, pc = 0.0f, ps = 0.0f;
m_mean:
    for (int i = 0; i < WIN_MOT; i++) {
        const float v = st.mh[(st.wp + i) & (WIN_MOT - 1)];
        mm += v;
        float c, s;
        f_cis((unsigned int)f_rad2ph(st.ph[(st.wp + i) & (WIN_PHS - 1)]), c, s);
        pc += c;
        ps += s;
    }
    mm *= (1.0f / (float)WIN_MOT);
    const float pm = f_atan2(ps, pc);
b_mean:
    for (int i = 0; i < WIN_BRT; i++) {
        const float v = st.bh[(st.bwp + i) & (WIN_BRT - 1)];
        bm += v;
        bt += v * ((float)i - 31.5f);
    }
    bm *= (1.0f / (float)WIN_BRT);
    const float slope = bt * (1.0f / 21840.0f);          // sum_i (i-31.5)^2, i = 0..63
    float smot = 0.0f, sbrt = 0.0f, sphs = 0.0f;
w_mot:
    for (int i = 0; i < WIN_MOT; i++) {
        const float v = st.mh[(st.wp + i) & (WIN_MOT - 1)] - mm;
        smot += v;
        mot.write(mkword(f2u(v), i == WIN_MOT - 1));
    }
w_brt:
    for (int i = 0; i < WIN_BRT; i++) {
        const float v = st.bh[(st.bwp + i) & (WIN_BRT - 1)] - bm - slope * ((float)i - 31.5f);
        sbrt += v;
        brt.write(mkword(f2u(v), i == WIN_BRT - 1));
    }
w_phs:
    for (int i = 0; i < WIN_PHS; i++) {
        const float v = f_wrap(st.ph[(st.wp + i) & (WIN_PHS - 1)] - pm);
        sphs += v;
        phs.write(mkword(f2u(v), i == WIN_PHS - 1));
    }
    const float np = (st.w_pk > 0) ? (float)st.w_pk : 1.0f;
    uint32_t t[TAG_WORDS];
    t[0] = TAG_MAGIC;
    t[1] = st.win_seq;
    t[2] = (uint32_t)st.last_ts(31, 0);
    t[3] = (uint32_t)st.last_ts(63, 32);
    t[4] = (st.w_pk & 0x00FFFFFFu) | (brt_full ? 0x80000000u : 0u);
    t[5] = f2u(st.w_rssi / np);
    t[6] = f2u(st.w_snr / np);
    t[7] = f2u(fs);
    t[8] = f2u(smot);
    t[9] = f2u(sbrt);
    t[10] = f2u(sphs);
    t[11] = f2u(st.w_cfo / np);
w_tag:
    for (int i = 0; i < TAG_WORDS; i++) tag.write(mkword(t[i], i == TAG_WORDS - 1));
    st.win_seq++;
    st.w_pk = 0;
    st.w_rssi = 0.0f;
    st.w_snr = 0.0f;
    st.w_cfo = 0.0f;
}

static void push_sample(FeatState& st, float a, float p, uint32_t hop, uint32_t br_dec, float fs,
                        hls::stream<axis32_t>& mot, hls::stream<axis32_t>& brt,
                        hls::stream<axis32_t>& phs, hls::stream<axis32_t>& tag) {
    st.mh[st.wp] = a;
    st.ph[st.wp] = p;
    st.wp = (st.wp + 1) & (WIN_MOT - 1);
    st.br_acc += a;
    st.br_n++;
    if ((uint32_t)st.br_n >= br_dec) {
        st.bh[st.bwp] = st.br_acc / (float)st.br_n;
        st.bwp = (st.bwp + 1) & (WIN_BRT - 1);
        st.br_acc = 0.0f;
        st.br_n = 0;
    }
    st.nsamp++;
    st.since_win++;
    st.last_a = a;
    st.last_p = p;
    if (st.nsamp >= (uint32_t)WIN_MOT && st.since_win >= hop) {
        emit_windows(st, fs, st.nsamp >= (uint32_t)WIN_BRT * br_dec, mot, brt, phs, tag);
        st.since_win = 0;
    }
}

void csi_feat(hls::stream<axis32_t>& csi_in, hls::stream<axis32_t>& mot, hls::stream<axis32_t>& brt,
              hls::stream<axis32_t>& phs, hls::stream<axis32_t>& tag,
              ap_uint<32> ctrl, ap_uint<32> bin_us, ap_uint<32> hop, ap_uint<32> br_dec,
              ap_int<32> k_ref, float fs_nom, float alpha, ap_uint<32> epoch,
              ap_uint<32>* n_in, ap_uint<32>* n_win, ap_uint<32>* n_samp) {
#pragma HLS INTERFACE mode=axis port=csi_in
#pragma HLS INTERFACE mode=axis port=mot
#pragma HLS INTERFACE mode=axis port=brt
#pragma HLS INTERFACE mode=axis port=phs
#pragma HLS INTERFACE mode=axis port=tag
#pragma HLS INTERFACE mode=s_axilite port=ctrl bundle=control
#pragma HLS INTERFACE mode=s_axilite port=bin_us bundle=control
#pragma HLS INTERFACE mode=s_axilite port=hop bundle=control
#pragma HLS INTERFACE mode=s_axilite port=br_dec bundle=control
#pragma HLS INTERFACE mode=s_axilite port=k_ref bundle=control
#pragma HLS INTERFACE mode=s_axilite port=fs_nom bundle=control
#pragma HLS INTERFACE mode=s_axilite port=alpha bundle=control
#pragma HLS INTERFACE mode=s_axilite port=epoch bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_in bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_win bundle=control
#pragma HLS INTERFACE mode=s_axilite port=n_samp bundle=control
#ifdef HLS_COSIM_HS
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control
#else
#pragma HLS INTERFACE mode=ap_ctrl_none port=return
#endif
    static FeatState st;
    static uint32_t nin = 0;
    static ap_uint<32> last_epoch = 0;
    static bool init = false;

    const uint32_t hp = (hop == 0) ? 10u : (uint32_t)hop;
    const uint32_t bd = (br_dec == 0) ? 10u : (uint32_t)br_dec;
    const uint32_t bus = (bin_us == 0) ? 50000u : (uint32_t)bin_us;
    const int kr = (k_ref == 0 || k_ref > 26 || k_ref < -26) ? 13 : (int)k_ref;
    const float fs = (fs_nom > 0.0f) ? fs_nom : 20.0f;
    const float al = (alpha > 0.0f && alpha <= 1.0f) ? alpha : 0.00390625f;

    // ---- read one CSI record ---------------------------------------------------
    uint32_t hdr[REC_HDR];
    float hr[64], hi[64];
    bool ok = true, last_at_end = false;
rd:
    for (int i = 0; i < REC_WORDS; i++) {
#pragma HLS PIPELINE II=1
        axis32_t w = csi_in.read();
        const uint32_t v = (uint32_t)w.data;
        if (i < REC_HDR) hdr[i] = v;
        else if ((i - REC_HDR) & 1) hi[(i - REC_HDR) >> 1] = u2f(v);
        else hr[(i - REC_HDR) >> 1] = u2f(v);
        if (i == REC_WORDS - 1) last_at_end = (w.last == 1);
        else if (w.last) ok = false;
    }
    if (!ok || !last_at_end || hdr[RW_MAGIC] != REC_MAGIC) {
        bool l = last_at_end;
    resync:
        for (int g = 0; g < 4 * REC_WORDS; g++) {
            if (l) break;
            axis32_t w = csi_in.read();
            l = (w.last == 1);
        }
        return;
    }
    // Restart (history cleared) at the first record after an ofdm_fe restart
    // (front-end capture seq 0) or an `epoch` change - evaluated after the read.
    if (!init || epoch != last_epoch || hdr[RW_RSV0] == 0) {
    clr:
        for (int i = 0; i < WIN_MOT; i++) {
            st.mh[i] = 0.0f;
            st.ph[i] = 0.0f;
            if (i < WIN_BRT) st.bh[i] = 0.0f;
        }
        st.wp = 0; st.bwp = 0; st.ema = 0.0f; st.have_ema = false;
        st.nsamp = 0; st.since_win = 0; st.win_seq = 0;
        st.br_acc = 0.0f; st.br_n = 0; st.have_bin = false; st.cur_bin = 0;
        st.acc_a = 0.0f; st.acc_p = 0.0f; st.acc_n = 0; st.last_a = 0.0f; st.last_p = 0.0f;
        st.w_pk = 0; st.w_rssi = 0.0f; st.w_snr = 0.0f; st.w_cfo = 0.0f; st.last_ts = 0;
        nin = 0;
        last_epoch = epoch;
        init = true;
    }
    nin++;
    *n_in = nin;
    const bool sig_ok = (hdr[RW_SIG] >> 24) & 1;
    if (ctrl[1] && !sig_ok) return;

    // ---- amplitude -------------------------------------------------------------
    float asum = 0.0f;
amp:
    for (int k = 0; k < 64; k++) {
        if (LTF_BIN[k] != 0) asum += sqrtf(hr[k] * hr[k] + hi[k] * hi[k]);
    }
    const float amp = asum * (1.0f / 52.0f);
    if (!st.have_ema) { st.ema = amp; st.have_ema = true; }
    else st.ema = st.ema + al * (amp - st.ema);
    const float a_norm = (st.ema > 0.0f) ? amp / st.ema - 1.0f : 0.0f;

    // ---- linear phase sanitisation -------------------------------------------------
    float sr = 0.0f, si = 0.0f;
slope:
    for (int kk = -26; kk < 26; kk++) {
        if (kk == -1 || kk == 0) continue;
        const int k0 = kk & 63, k1 = (kk + 1) & 63;
        // H[k1] * conj(H[k0])
        sr += hr[k1] * hr[k0] + hi[k1] * hi[k0];
        si += hi[k1] * hr[k0] - hr[k1] * hi[k0];
    }
    const float a = f_atan2(si, sr);
    const int pa = f_rad2ph(-a);
    float bsr = 0.0f, bsi = 0.0f;
offset:
    for (int kk = -26; kk <= 26; kk++) {
        if (kk == 0) continue;
        const int k = kk & 63;
        float c, s;
        f_cis((unsigned int)(pa * kk), c, s);
        bsr += hr[k] * c - hi[k] * s;
        bsi += hr[k] * s + hi[k] * c;
    }
    const float b = f_atan2(bsi, bsr);
    float c, s;
    f_cis((unsigned int)(pa * kr + f_rad2ph(-b)), c, s);
    const int kb = kr & 63;
    const float zr = hr[kb] * c - hi[kb] * s;
    const float zi = hr[kb] * s + hi[kb] * c;
    const float phi = f_atan2(zi, zr);

    // ---- per-window packet statistics ------------------------------------------
    st.w_pk++;
    st.w_rssi += u2f(hdr[RW_RSSI]);
    st.w_snr += u2f(hdr[RW_SNR]);
    st.w_cfo += u2f(hdr[RW_CFO]);
    const ap_uint<64> ts = ((ap_uint<64>)hdr[RW_TS_HI] << 32) | (ap_uint<64>)hdr[RW_TS_LO];
    st.last_ts = ts;

    // ---- binning -> samples -------------------------------------------------------
    if (ctrl[0]) {
        push_sample(st, a_norm, phi, hp, bd, fs, mot, brt, phs, tag);
    } else {
        const ap_uint<64> bin = ts / ((ap_uint<64>)bus * 20u);    // 20 samples per us
        if (!st.have_bin) {
            st.have_bin = true;
            st.cur_bin = bin;
        } else if (bin > st.cur_bin) {
            const float n = (st.acc_n > 0) ? (float)st.acc_n : 1.0f;
            if (st.acc_n > 0) push_sample(st, st.acc_a / n, st.acc_p / n, hp, bd, fs, mot, brt, phs, tag);
            ap_uint<64> gap = bin - st.cur_bin - 1;
            if (gap > 64) gap = 64;
        hold:
            for (int g = 0; g < 64; g++) {
                if ((ap_uint<64>)g >= gap) break;
                push_sample(st, st.last_a, st.last_p, hp, bd, fs, mot, brt, phs, tag);
            }
            st.cur_bin = bin;
            st.acc_a = 0.0f;
            st.acc_p = 0.0f;
            st.acc_n = 0;
        }
        st.acc_a += a_norm;
        st.acc_p += phi;
        st.acc_n++;
    }
    *n_win = st.win_seq;
    *n_samp = st.nsamp;
}
