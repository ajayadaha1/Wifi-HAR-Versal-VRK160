// ---------------------------------------------------------------------------
// golden_chain.cpp - bit-exact software model of the whole VRK160 PL + AIE
// pipeline, built from the SAME sources the hardware is synthesised from:
//
//   iq80 -> wifi_ddc -> ofdm_fe -> ofdm_be -> csi_feat -> [AIE cores] -> feat_pack
//
// (the AIE branches use the silicon-proven pure-C cores in aie/src/*_core.h).
// Build: make -C hls/tb   (g++ + the Vitis HLS headers, no HLS license needed)
//
// Usage: golden_chain <iq80.bin> <outdir> [options]
//   --nco <u32>      wifi_ddc nco_inc            --ddc-ctrl <u32>  wifi_ddc ctrl
//   --pkt-bins 0|1   csi_feat ctrl[0] (default 1) --sig-only 0|1   csi_feat ctrl[1] (default 1)
//   --hop N --brdec N --kref K --fs F --bin-us U  csi_feat parameters
//   --flush N        zero samples (80 MSPS) appended to drain the pipeline (default 4096)
//   --prefix <iq80.bin> [--prefix-cut N]   restart test: first play N words (default
//                    2/3 of the file + 1) of other traffic under epoch 1 through the SAME
//                    kernel state and streams, discard everything that reached the outputs
//                    (the host purge), then epoch 2 + PREROLL_WORDS zeros + the vectors.
//                    The outputs must be byte-identical to a clean run.
// Outputs in <outdir>: csi_rec.bin (REC_WORDS/record, as s2mm_csi writes them),
// feat.bin (FEAT_WORDS/record, as s2mm_feat writes them), aie_{mot,brt,phs}_in.txt
// (first window, for x86sim), and per-kernel stream dumps s20/cap/csi/tag/mot/brt/phs.bin
// used by the HLS C/RTL co-simulation testbenches (hls/tb/tb_kernel.cpp).
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../wifi_ddc/wifi_ddc.cpp"
#include "../ofdm_rx/ofdm_fe.cpp"
#include "../ofdm_rx/ofdm_be.cpp"
#include "../csi_feat/csi_feat.cpp"
#include "../csi_feat/feat_pack.cpp"

namespace aie {
#include "../../aie/src/fir_core.h"
#include "../../aie/src/stats_core.h"
#include "../../aie/src/dft_core.h"
}

static void dump_words(FILE* f, const std::vector<uint32_t>& v) {
    if (f && !v.empty()) std::fwrite(v.data(), 4, v.size(), f);
}
static const char* capturing_hint(hls::stream<axis32_t>& cap) {
    return cap.size() ? "mid-capture" : "idle/plateau";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <iq80.bin> <outdir> [--nco u32] [--ddc-ctrl u32] [--pkt-bins 0|1] "
                             "[--sig-only 0|1] [--hop N] [--brdec N] [--kref K] [--fs F] [--bin-us U] [--flush N]\n",
                     argv[0]);
        return 2;
    }
    const std::string in_path = argv[1], od = argv[2];
    uint32_t nco = 0, ddc_ctrl = 0, pkt_bins = 1, sig_only = 1, hop = 10, brdec = 10, bin_us = 50000;
    int kref = 13, flush = 4096;
    std::string prefix_path;          // --prefix: "stale" traffic played under epoch 1 first
    long prefix_cut = -1;
    float fs = 20.0f;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        const char* v = argv[i + 1];
        if (a == "--nco") nco = (uint32_t)std::strtoul(v, nullptr, 0);
        else if (a == "--ddc-ctrl") ddc_ctrl = (uint32_t)std::strtoul(v, nullptr, 0);
        else if (a == "--pkt-bins") pkt_bins = std::atoi(v);
        else if (a == "--sig-only") sig_only = std::atoi(v);
        else if (a == "--hop") hop = std::atoi(v);
        else if (a == "--brdec") brdec = std::atoi(v);
        else if (a == "--kref") kref = std::atoi(v);
        else if (a == "--fs") fs = (float)std::atof(v);
        else if (a == "--bin-us") bin_us = std::atoi(v);
        else if (a == "--flush") flush = std::atoi(v);
        else if (a == "--prefix") prefix_path = v;
        else if (a == "--prefix-cut") prefix_cut = std::atol(v);
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    FILE* fi = std::fopen(in_path.c_str(), "rb");
    if (!fi) { std::perror(in_path.c_str()); return 1; }
    std::vector<uint32_t> iq;
    {
        uint32_t b[4096];
        size_t n;
        while ((n = std::fread(b, 4, 4096, fi)) > 0) iq.insert(iq.end(), b, b + n);
    }
    std::fclose(fi);
    for (int i = 0; i < flush; i++) iq.push_back(0);
    while (iq.size() % 4) iq.push_back(0);
    // --prefix: emulate a pipeline that ran other traffic before the self-test (see usage)
    std::vector<uint32_t> pre;
    if (!prefix_path.empty()) {
        FILE* fp = std::fopen(prefix_path.c_str(), "rb");
        if (!fp) { std::perror(prefix_path.c_str()); return 1; }
        uint32_t b[4096];
        size_t n;
        while ((n = std::fread(b, 4, 4096, fp)) > 0) pre.insert(pre.end(), b, b + n);
        std::fclose(fp);
        const size_t cut = prefix_cut >= 0 ? (size_t)prefix_cut : pre.size() * 2 / 3 + 1;
        pre.resize(std::min(cut, pre.size()));              // stop mid-stream, no flush
        iq.insert(iq.begin(), (size_t)PREROLL_WORDS, 0u);
    }

    auto opn = [&](const char* n) { return std::fopen((od + "/" + n).c_str(), "wb"); };
    FILE *f_s20 = opn("s20.bin"), *f_cap = opn("cap.bin"), *f_csi = opn("csi.bin"), *f_rec = opn("csi_rec.bin");
    FILE *f_mot = opn("mot.bin"), *f_brt = opn("brt.bin"), *f_phs = opn("phs.bin"), *f_tag = opn("tag.bin");
    FILE *f_amo = opn("aie_mot_out.bin"), *f_abr = opn("aie_brt_out.bin"), *f_aph = opn("aie_phs_out.bin");
    FILE* f_feat = opn("feat.bin");
    if (!f_feat) { std::fprintf(stderr, "cannot write into %s\n", od.c_str()); return 1; }

    hls::stream<axis32_t> in80("in80"), s20("s20"), cap("cap"), csi("csi"), rec("rec");
    hls::stream<axis32_t> mot("mot"), brt("brt"), phs("phs"), tag("tag");
    hls::stream<axis32_t> amo("amo"), abr("abr"), aph("aph"), feat("feat");
    ap_uint<32> n_trig = 0, p_now = 0, n_pkt = 0, n_ok = 0, last_sig = 0, n_res = 0, n_in = 0, n_win = 0, n_samp = 0;
    uint32_t epoch = 1;                         // != reset value 0 -> every kernel restarts
    const uint32_t be_ctrl = 1;                 // rec_en, all packets, adv = default
    const uint32_t cf_ctrl = (pkt_bins ? 1u : 0u) | (sig_only ? 2u : 0u);
    long n_rec = 0, n_feat = 0;

    // One 80 MSPS input word through the whole chain. keep == false: the outputs are
    // consumed and discarded (the prefix of --prefix, i.e. the host purge).
    bool keep = true;
    auto dump_words = [&](FILE* f, const std::vector<uint32_t>& v) { if (keep) ::dump_words(f, v); };
    auto step = [&](uint32_t word) {
        in80.write(mkword(word, false));
        wifi_ddc(in80, s20, nco, ddc_ctrl, epoch);          // one input sample per call
        if (s20.empty()) return;                             // an output every 4th sample
        axis32_t smp = s20.read();
        if (f_s20 && keep) { uint32_t d = (uint32_t)smp.data; std::fwrite(&d, 4, 1, f_s20); }
        hls::stream<axis32_t> one("one");
        one.write(smp);
        ofdm_fe(one, cap, 0.49f, 0.0f, 24, 480, epoch, &n_trig, &p_now);
        while ((int)cap.size() >= CAP_WORDS) {
            std::vector<uint32_t> c, rw;
            hls::stream<axis32_t> cap2("cap2");
            for (int k = 0; k < CAP_WORDS; k++) { axis32_t x = cap.read(); c.push_back((uint32_t)x.data); cap2.write(x); }
            dump_words(f_cap, c);
            ofdm_be(cap2, csi, rec, be_ctrl, epoch, &n_pkt, &n_ok, &last_sig, &n_res);
            while (!rec.empty()) rw.push_back((uint32_t)rec.read().data);
            if (keep) n_rec += (long)(rw.size() / REC_WORDS);
            dump_words(f_rec, rw);
        }
        while ((int)csi.size() >= REC_WORDS) {
            std::vector<uint32_t> c;
            hls::stream<axis32_t> csi2("csi2");
            for (int k = 0; k < REC_WORDS; k++) { axis32_t x = csi.read(); c.push_back((uint32_t)x.data); csi2.write(x); }
            dump_words(f_csi, c);
            csi_feat(csi2, mot, brt, phs, tag, cf_ctrl, bin_us, hop, brdec, kref, fs, 0.0f, epoch,
                     &n_in, &n_win, &n_samp);
        }
        // AI Engine branches (the pure-C cores, exactly as compiled for the array)
        while ((int)mot.size() >= WIN_MOT && (int)brt.size() >= WIN_BRT && (int)phs.size() >= WIN_PHS) {
            float xm[WIN_MOT], ym[WIN_MOT], om[OUT_MOT], xb[WIN_BRT], ob[OUT_BRT], xp[WIN_PHS], op[OUT_PHS];
            std::vector<uint32_t> dm, db, dp, vm, vb, vp, tg, fw;
            for (int k = 0; k < WIN_MOT; k++) { uint32_t u = (uint32_t)mot.read().data; dm.push_back(u); xm[k] = u2f(u); }
            for (int k = 0; k < WIN_BRT; k++) { uint32_t u = (uint32_t)brt.read().data; db.push_back(u); xb[k] = u2f(u); }
            for (int k = 0; k < WIN_PHS; k++) { uint32_t u = (uint32_t)phs.read().data; dp.push_back(u); xp[k] = u2f(u); }
            dump_words(f_mot, dm);
            dump_words(f_brt, db);
            dump_words(f_phs, dp);
            if (n_feat == 0 && keep) {          // first windows -> x86sim/aiesim PLIO input files
                auto wr = [&](const char* n, const float* x, int len) {
                    FILE* t = std::fopen((od + "/" + n).c_str(), "w");
                    for (int k = 0; k < len; k++) std::fprintf(t, "%.9e\n", x[k]);
                    std::fclose(t);
                };
                wr("aie_mot_in.txt", xm, WIN_MOT);
                wr("aie_brt_in.txt", xb, WIN_BRT);
                wr("aie_phs_in.txt", xp, WIN_PHS);
            }
            aie::fir_core(xm, ym);
            aie::stats_core(ym, om);
            aie::dft_mag_core(xb, ob);
            aie::stats_core(xp, op);
            for (int k = 0; k < OUT_MOT; k++) { amo.write(mkword(f2u(om[k]), k == OUT_MOT - 1)); vm.push_back(f2u(om[k])); }
            for (int k = 0; k < OUT_BRT; k++) { abr.write(mkword(f2u(ob[k]), k == OUT_BRT - 1)); vb.push_back(f2u(ob[k])); }
            for (int k = 0; k < OUT_PHS; k++) { aph.write(mkword(f2u(op[k]), k == OUT_PHS - 1)); vp.push_back(f2u(op[k])); }
            dump_words(f_amo, vm);
            dump_words(f_abr, vb);
            dump_words(f_aph, vp);
            hls::stream<axis32_t> tag2("tag2");
            for (int k = 0; k < TAG_WORDS; k++) { axis32_t x = tag.read(); tg.push_back((uint32_t)x.data); tag2.write(x); }
            dump_words(f_tag, tg);
            feat_pack(tag2, amo, abr, aph, feat);
            while (!feat.empty()) fw.push_back((uint32_t)feat.read().data);
            dump_words(f_feat, fw);
            if (keep) n_feat++;
        }
    };
    if (!pre.empty()) {
        keep = false;
        for (uint32_t wd : pre) step(wd);
        std::printf("PREFIX: %zu words of other traffic under epoch 1 (fe triggers %u, fe %s), outputs "
                    "discarded; %zu capture words left in flight\n", pre.size(), (unsigned)n_trig,
                    capturing_hint(cap), (size_t)cap.size());
        keep = true;
        epoch = 2;
    }
    for (size_t i = 0; i < iq.size(); i++) step(iq[i]);
    FILE* all[] = {f_s20, f_cap, f_csi, f_rec, f_mot, f_brt, f_phs, f_tag, f_amo, f_abr, f_aph, f_feat};
    for (FILE* f : all) if (f) std::fclose(f);
    std::printf("GOLDEN_CHAIN: in80=%zu samples  triggers=%u  packets=%u  sig_ok=%u  resync=%u  "
                "csi_rec=%ld  csi_feat_in=%u samples=%u windows=%u  feat=%ld\n",
                iq.size(), (unsigned)n_trig, (unsigned)n_pkt, (unsigned)n_ok, (unsigned)n_res, n_rec,
                (unsigned)n_in, (unsigned)n_samp, (unsigned)n_win, n_feat);
    return 0;
}
