// ---------------------------------------------------------------------------
// tb_kernel.cpp - one C-sim / C-RTL co-sim testbench for every PL kernel.
// Build with -DK_WIFI_DDC | -DK_OFDM_FE | -DK_OFDM_BE | -DK_CSI_FEAT |
// -DK_FEAT_PACK | -DK_IQ_MUX | -DK_MOVERS. Stimulus and expected outputs are the
// per-kernel stream dumps written by golden_chain (env TBDIR, set by build_kernels.sh
// to build/vectors/quick; csi_feat/feat_pack use TBDIR_DEMO = build/vectors/demo). Every output
// word is compared bit-exactly. Prints "TB_<kernel> PASS" and returns 0.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../common/wifi_common.h"

static std::vector<uint32_t> rd(const std::string& p, size_t max_words = (size_t)-1) {
    std::vector<uint32_t> v;
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "TB: cannot open %s\n", p.c_str()); return v; }
    uint32_t w;
    while (v.size() < max_words && std::fread(&w, 4, 1, f) == 1) v.push_back(w);
    std::fclose(f);
    return v;
}

static std::string dir(const char* env, const char* dflt) {
    const char* e = std::getenv(env);
    return std::string(e ? e : dflt);
}

static int compare(const char* what, hls::stream<axis32_t>& s, const std::vector<uint32_t>& exp, size_t n) {
    size_t got = 0, bad = 0;
    while (!s.empty() && got < n) {
        uint32_t v = (uint32_t)s.read().data;
        if (got < exp.size() && v != exp[got]) {
            if (bad < 5) std::printf("  %s word %zu: got 0x%08x exp 0x%08x\n", what, got, v, exp[got]);
            bad++;
        }
        got++;
    }
    std::printf("  %s: %zu/%zu words, %zu mismatches\n", what, got, n, bad);
    return (got == n && bad == 0) ? 0 : 1;
}

#if defined(K_WIFI_DDC)
void wifi_ddc(hls::stream<axis32_t>&, hls::stream<axis32_t>&, ap_uint<32>, ap_uint<32>, ap_uint<32>);
int main() {
    const std::string d = dir("TBDIR", "/tmp/vrk160_golden_quick");
    const size_t N = 8192;                                  // outputs
    auto in = rd(d + "/iq80.bin", 4 * N), exp = rd(d + "/s20.bin", N);
    const uint32_t nco = (uint32_t)std::strtoul(dir("TB_NCO", "4026531840").c_str(), nullptr, 0);
    if (in.size() < 4 * N || exp.size() < N) return 1;
    hls::stream<axis32_t> si("in"), so("out");
    for (size_t i = 0; i < 4 * N; i++) {                   // II=1: one input sample per call
        si.write(mkword(in[i], false));
        wifi_ddc(si, so, nco, 0, 1);
    }
    int rc = compare("s20", so, exp, N);
    std::printf("TB_WIFI_DDC %s\n", rc ? "FAIL" : "PASS");
    return rc;
}
#elif defined(K_OFDM_FE)
void ofdm_fe(hls::stream<axis32_t>&, hls::stream<axis32_t>&, float, float, ap_uint<32>, ap_uint<32>, ap_uint<32>,
             ap_uint<32>*, ap_uint<32>*);
int main() {
    const std::string d = dir("TBDIR", "/tmp/vrk160_golden_quick");
    const size_t N = 12000;                                 // ~12 packets
    auto in = rd(d + "/s20.bin", N), exp = rd(d + "/cap.bin");
    if (in.size() < N) return 1;
    hls::stream<axis32_t> si("in"), so("cap");
    ap_uint<32> ntrig, pnow;
    for (size_t i = 0; i < N; i++) {
        si.write(mkword(in[i], false));
        ofdm_fe(si, so, 0.49f, 0.0f, 24, 480, 1, &ntrig, &pnow);
    }
    const size_t ncap = so.size() / CAP_WORDS;
    std::printf("  captures produced: %zu (triggers %u)\n", ncap, (unsigned)ntrig);
    // absolute timestamps (trailer words 5/6) may be offset by a constant in RTL
    // co-sim (the free-running kernel runs before the TB writes `epoch`): compare
    // them relative to the first capture, everything else bit-exactly.
    std::vector<uint32_t> got;
    while (!so.empty()) got.push_back((uint32_t)so.read().data);
    int rc = (ncap >= 10) ? 0 : 1;
    size_t bad = 0;
    for (size_t i = 0; i < ncap * CAP_WORDS && i < exp.size(); i++) {
        const size_t w = i % CAP_WORDS;
        uint32_t g = got[i], e = exp[i];
        if (w == N_CAP + 5) { g -= got[N_CAP + 5]; e -= exp[N_CAP + 5]; }
        if (w == N_CAP + 6) continue;
        if (g != e) { if (bad < 5) std::printf("  cap word %zu: got 0x%08x exp 0x%08x\n", i, g, e); bad++; }
    }
    std::printf("  cap: %zu words, %zu mismatches (timestamps relative)\n", ncap * CAP_WORDS, bad);
    if (bad) rc = 1;
    std::printf("TB_OFDM_FE %s\n", rc ? "FAIL" : "PASS");
    return rc;
}
#elif defined(K_OFDM_BE)
void ofdm_be(hls::stream<axis32_t>&, hls::stream<axis32_t>&, hls::stream<axis32_t>&, ap_uint<32>, ap_uint<32>,
             ap_uint<32>*, ap_uint<32>*, ap_uint<32>*, ap_uint<32>*);
int main() {
    const std::string d = dir("TBDIR", "/tmp/vrk160_golden_quick");
    const size_t K = 12;
    auto in = rd(d + "/cap.bin", K * CAP_WORDS), exp = rd(d + "/csi.bin", K * REC_WORDS);
    if (in.size() < K * CAP_WORDS) return 1;
    hls::stream<axis32_t> si("cap"), sc("csi"), sr("rec");
    ap_uint<32> np, nok, ls, nr;
    for (size_t k = 0; k < K; k++) {
        for (int i = 0; i < CAP_WORDS; i++) si.write(mkword(in[k * CAP_WORDS + i], i == CAP_WORDS - 1));
        ofdm_be(si, sc, sr, 1, 1, &np, &nok, &ls, &nr);
    }
    std::printf("  packets %u sig_ok %u resync %u\n", (unsigned)np, (unsigned)nok, (unsigned)nr);
    int rc = compare("csi", sc, exp, K * REC_WORDS);
    rc |= compare("rec", sr, exp, K * REC_WORDS);
    std::printf("TB_OFDM_BE %s\n", rc ? "FAIL" : "PASS");
    return rc;
}
#elif defined(K_CSI_FEAT)
void csi_feat(hls::stream<axis32_t>&, hls::stream<axis32_t>&, hls::stream<axis32_t>&, hls::stream<axis32_t>&,
              hls::stream<axis32_t>&, ap_uint<32>, ap_uint<32>, ap_uint<32>, ap_uint<32>, ap_int<32>, float, float,
              ap_uint<32>, ap_uint<32>*, ap_uint<32>*, ap_uint<32>*);
int main() {
    const std::string d = dir("TBDIR_DEMO", "/tmp/vrk160_golden_demo");
    const size_t R = 300;                                   // -> windows at samples 256, 266, ... 296
    auto in = rd(d + "/csi.bin", R * REC_WORDS);
    if (in.size() < R * REC_WORDS) return 1;
    hls::stream<axis32_t> si("csi"), m("mot"), b("brt"), p("phs"), t("tag");
    ap_uint<32> nin, nwin, nsamp;
    for (size_t r = 0; r < R; r++) {
        for (int i = 0; i < REC_WORDS; i++) si.write(mkword(in[r * REC_WORDS + i], i == REC_WORDS - 1));
        csi_feat(si, m, b, p, t, 3, 50000, 10, 10, 13, 20.0f, 0.0f, 1, &nin, &nwin, &nsamp);
    }
    const size_t W = nwin;
    std::printf("  records %u samples %u windows %zu\n", (unsigned)nin, (unsigned)nsamp, W);
    int rc = (W >= 4) ? 0 : 1;
    rc |= compare("mot", m, rd(d + "/mot.bin", W * WIN_MOT), W * WIN_MOT);
    rc |= compare("brt", b, rd(d + "/brt.bin", W * WIN_BRT), W * WIN_BRT);
    rc |= compare("phs", p, rd(d + "/phs.bin", W * WIN_PHS), W * WIN_PHS);
    // tag words 8..10 are float checksums of the windows (debug aid): the RTL may
    // place that running sum on a different adder structure (1-ULP differences),
    // so they are compared with a relative tolerance; every other word bit-exactly.
    {
        auto et = rd(d + "/tag.bin", W * TAG_WORDS);
        size_t got = 0, bad = 0;
        while (!t.empty()) {
            const uint32_t v = (uint32_t)t.read().data;
            const size_t w = got % TAG_WORDS;
            bool ok = (got < et.size()) && (v == et[got]);
            if (!ok && got < et.size() && w >= 8 && w <= 10) {
                const float a = u2f(v), e = u2f(et[got]);
                const float den = (e < 0 ? -e : e) + 1e-30f;
                ok = ((a - e) < 0 ? (e - a) : (a - e)) / den < 1e-5f;
            }
            if (!ok) { if (bad < 5) std::printf("  tag word %zu: got 0x%08x exp 0x%08x\n", got, v, got < et.size() ? et[got] : 0u); bad++; }
            got++;
        }
        std::printf("  tag: %zu/%zu words, %zu mismatches (checksums rel. 1e-5)\n", got, W * TAG_WORDS, bad);
        if (got != W * TAG_WORDS || bad) rc |= 1;
    }
    std::printf("TB_CSI_FEAT %s\n", rc ? "FAIL" : "PASS");
    return rc;
}
#elif defined(K_FEAT_PACK)
void feat_pack(hls::stream<axis32_t>&, hls::stream<axis32_t>&, hls::stream<axis32_t>&, hls::stream<axis32_t>&,
               hls::stream<axis32_t>&);
int main() {
    const std::string d = dir("TBDIR_DEMO", "/tmp/vrk160_golden_demo");
    const size_t W = 8;
    auto tg = rd(d + "/tag.bin", W * TAG_WORDS), mo = rd(d + "/aie_mot_out.bin", W * OUT_MOT);
    auto br = rd(d + "/aie_brt_out.bin", W * OUT_BRT), ph = rd(d + "/aie_phs_out.bin", W * OUT_PHS);
    if (tg.size() < W * TAG_WORDS) return 1;
    hls::stream<axis32_t> t("tag"), m("mot"), b("brt"), p("phs"), o("out");
    for (size_t w = 0; w < W; w++) {
        for (int i = 0; i < TAG_WORDS; i++) t.write(mkword(tg[w * TAG_WORDS + i], i == TAG_WORDS - 1));
        for (int i = 0; i < OUT_MOT; i++) m.write(mkword(mo[w * OUT_MOT + i], i == OUT_MOT - 1));
        for (int i = 0; i < OUT_BRT; i++) b.write(mkword(br[w * OUT_BRT + i], i == OUT_BRT - 1));
        for (int i = 0; i < OUT_PHS; i++) p.write(mkword(ph[w * OUT_PHS + i], i == OUT_PHS - 1));
        feat_pack(t, m, b, p, o);
    }
    int rc = compare("feat", o, rd(d + "/feat.bin", W * FEAT_WORDS), W * FEAT_WORDS);
    std::printf("TB_FEAT_PACK %s\n", rc ? "FAIL" : "PASS");
    return rc;
}
#elif defined(K_IQ_MUX)
void iq_mux(hls::stream<ap_uint<32> >&, hls::stream<axis32_t>&, hls::stream<axis32_t>&, ap_uint<32>,
            ap_uint<1>*, ap_uint<32>*);
int main() {
    hls::stream<ap_uint<32> > rf("rf");
    hls::stream<axis32_t> pb("pb"), o("out");
    ap_uint<1> rstn = 0;
    ap_uint<32> cnt = 0;
    int rc = 0;
    for (int i = 0; i < 64; i++) { rf.write(0x1000 + i); pb.write(mkword(0x2000 + i, i == 63)); }
    for (int i = 0; i < 80; i++) iq_mux(rf, pb, o, 1, &rstn, &cnt);      // playback, rf not drained
    for (int i = 0; i < 64; i++) { if (o.empty() || (uint32_t)o.read().data != (uint32_t)(0x2000 + i)) rc = 1; }
    if (rstn != 0 || cnt != 0) rc = 1;
    for (int i = 0; i < 80; i++) iq_mux(rf, pb, o, 4, &rstn, &cnt);      // live rf, RF reset released
    for (int i = 0; i < 64; i++) { if (o.empty() || (uint32_t)o.read().data != (uint32_t)(0x1000 + i)) rc = 1; }
    if (rstn != 1 || cnt != 64) rc = 1;
    std::printf("TB_IQ_MUX %s\n", rc ? "FAIL" : "PASS");
    return rc;
}
#elif defined(K_MOVERS)
extern "C" void mm2s(const ap_uint<32>*, hls::stream<ap_axiu<32, 0, 0, 0> >&, int);
extern "C" void s2mm(ap_uint<32>*, hls::stream<ap_axiu<32, 0, 0, 0> >&, int, int);
int main() {
    static ap_uint<32> a[4096], b[4096], c[4096];  // = the m_axi depth=4096 (co-sim)
    for (int i = 0; i < 4096; i++) { a[i] = 0xABC00000u + i; b[i] = 0; c[i] = 0; }
    hls::stream<ap_axiu<32, 0, 0, 0> > s("s");
    mm2s(a, s, 4000);
    s2mm(b, s, 1000, 0);                           // blocking: exactly 1000 words
    s2mm(c, s, 4095, 64);                          // idle exit after the remaining 3000
    int rc = 0;
    for (int i = 0; i < 1000; i++) if (b[i] != a[i]) rc = 1;
    for (int i = 0; i < 3000; i++) if (c[i] != a[1000 + i]) rc = 1;
    if (b[1000] != 1000 || c[4095] != 3000) rc = 1;
    std::printf("TB_MOVERS %s (counts %u %u)\n", rc ? "FAIL" : "PASS", (unsigned)b[1000], (unsigned)c[4095]);
    return rc;
}
#else
#error "define one of K_WIFI_DDC K_OFDM_FE K_OFDM_BE K_CSI_FEAT K_FEAT_PACK K_IQ_MUX K_MOVERS"
#endif
