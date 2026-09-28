// ---------------------------------------------------------------------------
// host_wifi.cpp - XRT host for the VRK160 WiFi-CSI HAR design.
//
//   host_wifi selftest <xclbin> <vecdir>      DDR playback -> full PL/AIE chain, compare
//                                             CSI records + AIE features with the
//                                             golden model (hls/tb/golden_chain)
//   host_wifi play     <xclbin> <vecdir> [--loop] [--speed X] [--csi-every N]
//                                             paced playback of a recorded scenario,
//                                             streams FEAT/CSI/STAT lines (dashboard)
//   host_wifi live     <xclbin> [--chan N] [--bin-us U] [--thr T] [--pmin P] [--csi-every N]
//                                             live RF from ADC tile 200 (2.4 GHz ch 1..11)
//   host_wifi status   [xclbin]                register dump
//
// Prerequisite (segmented configuration): the PL + AIE image is loaded, i.e.
//   fpgautil -b wifi_har_vrk160.pdi -o wifi_har_vrk160.dtbo   (wifi-har-autorun.sh does it)
// Every session purges stale records, bumps the epochs and plays a zero pre-roll
// (docs/OFDM_RX.md 7.1), so runs are repeatable after any previous traffic.
// XRT owns only the three data movers (mm2s_iq, s2mm_csi, s2mm_feat, XRT buffers:
// NoC-valid DDR, see VCK190 PROJECT_STATE #28). The free-running PL kernels and
// the RF front end are configured over /dev/mem at the pinned addresses in pl_map.h.
// Output lines (one per record, parsed by live/live_dashboard.py --source exec):
//   FEAT,seq,t_s,n_pkts,brt_full,rssi_dbfs,snr_db,fs,cfo_hz,mot0..2,brt0..32,phs0..2
//   CSI,seq,t_s,sig_ok,rate,len,cfo_hz,rssi_dbfs,snr_db,|H|[-32..31]
//   STAT,rf_count,n_trig,n_pkt,n_sig_ok,n_resync,feat_in,feat_win,p_now_dbfs
// Build (PetaLinux SDK): $CXX -std=c++17 -O2 host_wifi.cpp -o host_wifi -lxrt_coreutil -luuid -pthread
// ---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/experimental/xrt_graph.h"

#include "pl_map.h"

using namespace std::chrono_literals;

// ---------------------------------------------------------------- utilities --
static std::mutex g_out;
static void emit(const std::string& s) {
    std::lock_guard<std::mutex> l(g_out);
    std::fputs(s.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}
static inline float u2f(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
static inline uint32_t f2u(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
static double dbfs(double pw_lsb2) { return 10.0 * std::log10(std::max(pw_lsb2, 1e-3) / (32768.0 * 32768.0)); }

static std::vector<uint32_t> read_words(const std::string& p) {
    std::vector<uint32_t> v;
    std::ifstream f(p, std::ios::binary);
    if (!f) return v;
    f.seekg(0, std::ios::end);
    const size_t n = (size_t)f.tellg() / 4;
    f.seekg(0);
    v.resize(n);
    f.read(reinterpret_cast<char*>(v.data()), (std::streamsize)(n * 4));
    return v;
}
// minimal "key": number extractor for vectors.json
static double json_num(const std::string& path, const std::string& key, double dflt) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return dflt;
    const auto c = s.find(':', k);
    return c == std::string::npos ? dflt : std::strtod(s.c_str() + c + 1, nullptr);
}

// ------------------------------------------------------------ PL registers --
struct Pl {
    volatile uint8_t* b = nullptr;
    int fd = -1;
    bool open_() {
        fd = ::open("/dev/mem", O_RDWR | O_SYNC);
        if (fd < 0) { std::perror("open /dev/mem"); return false; }
        void* m = ::mmap(nullptr, pl::SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pl::BASE);
        if (m == MAP_FAILED) { std::perror("mmap PL control"); return false; }
        b = static_cast<volatile uint8_t*>(m);
        return true;
    }
    ~Pl() { if (b) ::munmap((void*)b, pl::SPAN); if (fd >= 0) ::close(fd); }
    void w(uint32_t off, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(b + off) = v; }
    uint32_t r(uint32_t off) { return *reinterpret_cast<volatile uint32_t*>(b + off); }
    void wf(uint32_t off, float v) { w(off, f2u(v)); }
};

struct RxCfg {
    uint32_t nco_inc = 0, ddc_ctrl = 0;
    float thr2 = 0.49f, p_min = 0.0f;
    uint32_t n_plat = 24, holdoff = 480;
    uint32_t be_ctrl = pl::BE_REC_EN;
    uint32_t feat_ctrl = pl::FEAT_SIG_ONLY;
    uint32_t bin_us = 50000, hop = 10, br_dec = 10;
    int32_t k_ref = 13;
    float fs_nom = 20.0f, alpha = 0.0f;
};

// program every free-running kernel, then bump every epoch (clean deterministic restart)
static void configure(Pl& p, const RxCfg& c) {
    using namespace pl;
    p.w(WIFI_DDC + ddc::NCO_INC, c.nco_inc);
    p.w(WIFI_DDC + ddc::CTRL, c.ddc_ctrl);
    p.wf(OFDM_FE + fe::THR2, c.thr2);
    p.wf(OFDM_FE + fe::P_MIN, c.p_min);
    p.w(OFDM_FE + fe::N_PLAT, c.n_plat);
    p.w(OFDM_FE + fe::HOLDOFF, c.holdoff);
    p.w(OFDM_BE + be::CTRL, c.be_ctrl);
    p.w(CSI_FEAT + feat::CTRL, c.feat_ctrl);
    p.w(CSI_FEAT + feat::BIN_US, c.bin_us);
    p.w(CSI_FEAT + feat::HOP, c.hop);
    p.w(CSI_FEAT + feat::BR_DEC, c.br_dec);
    p.w(CSI_FEAT + feat::K_REF, (uint32_t)c.k_ref);
    p.wf(CSI_FEAT + feat::FS_NOM, c.fs_nom);
    p.wf(CSI_FEAT + feat::ALPHA, c.alpha);
    const uint32_t e = p.r(CSI_FEAT + feat::EPOCH) + 1u;
    p.w(WIFI_DDC + ddc::EPOCH, e);
    p.w(OFDM_FE + fe::EPOCH, e);
    p.w(OFDM_BE + be::EPOCH, e);
    p.w(CSI_FEAT + feat::EPOCH, e);
}

static void print_status(Pl& p) {
    using namespace pl;
    char s[256];
    std::snprintf(s, sizeof s, "STAT,%u,%u,%u,%u,%u,%u,%u,%.1f",
                  p.r(IQ_MUX + iq_mux::RF_COUNT), p.r(OFDM_FE + fe::N_TRIG), p.r(OFDM_BE + be::N_PKT),
                  p.r(OFDM_BE + be::N_SIG_OK), p.r(OFDM_BE + be::N_RESYNC), p.r(CSI_FEAT + feat::N_IN),
                  p.r(CSI_FEAT + feat::N_WIN), dbfs(u2f(p.r(OFDM_FE + fe::P_NOW)) / 48.0));
    emit(s);
}

// ------------------------------------------------------------ record decode --
static std::string csi_line(const uint32_t* r) {
    using namespace pl;
    const uint32_t sig = r[RW_SIG];
    const double t = (double)(((uint64_t)r[RW_TS_HI] << 32) | r[RW_TS_LO]) / FS20;
    char h[256];
    std::snprintf(h, sizeof h, "CSI,%u,%.6f,%u,%u,%u,%.0f,%.1f,%.1f", r[RW_SEQ], t, (sig >> 24) & 1u,
                  (r[RW_FMT] >> 16) & 0xFFu, r[RW_LEN], u2f(r[RW_CFO]), dbfs(u2f(r[RW_RSSI])),
                  10.0 * std::log10(std::max(1e-6f, u2f(r[RW_SNR]))));
    std::string s(h);
    for (int kk = -32; kk < 32; kk++) {
        const int k = kk & 63;
        const float re = u2f(r[REC_HDR + 2 * k]), im = u2f(r[REC_HDR + 2 * k + 1]);
        char v[24];
        std::snprintf(v, sizeof v, ",%.4g", std::sqrt(re * re + im * im));
        s += v;
    }
    return s;
}

static std::string feat_line(const uint32_t* f) {
    using namespace pl;
    const double t = (double)(((uint64_t)f[3] << 32) | f[2]) / FS20;
    char h[256];
    std::snprintf(h, sizeof h, "FEAT,%u,%.6f,%u,%u,%.1f,%.1f,%.3f,%.0f", f[1], t, f[4] & 0x00FFFFFFu, f[4] >> 31,
                  dbfs(u2f(f[5])), 10.0 * std::log10(std::max(1e-6f, u2f(f[6]))), u2f(f[7]), u2f(f[11]));
    std::string s(h);
    for (int i = 0; i < OUT_MOT + OUT_BRT + OUT_PHS; i++) {
        char v[24];
        std::snprintf(v, sizeof v, ",%.6g", u2f(f[TAG_WORDS + i]));
        s += v;
    }
    return s;
}

// ------------------------------------------------------------------- XRT --
struct Xrt {
    xrt::device dev;
    xrt::uuid uuid;
    xrt::kernel mm2s, s2mm_csi, s2mm_feat;
    explicit Xrt(const std::string& xclbin) : dev(0) {
        uuid = dev.load_xclbin(xclbin);
        mm2s = xrt::kernel(dev, uuid, "mm2s:{mm2s_iq}");
        s2mm_csi = xrt::kernel(dev, uuid, "s2mm:{s2mm_csi}");
        s2mm_feat = xrt::kernel(dev, uuid, "s2mm:{s2mm_feat}");
        // The packaged AIE CDO normally free-runs feature_graph already; make sure.
        try {
            xrt::graph g(dev, uuid, "feature_graph");
            g.run(0);
            std::fprintf(stderr, "feature_graph: started (run forever)\n");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "feature_graph: already running from the CDO (%s)\n", e.what());
        }
    }
};

static int idle_cycles(double seconds) { return (int)std::min(seconds * pl::PL_CLK_HZ, 2.0e9); }

// One s2mm run into `bo` (size + 1 words): returns the number of words received
// (the kernel writes it to mem[size]); idle_s > 0 bounds the wait on a silent stream.
static xrt::run s2mm_start(xrt::kernel& k, xrt::bo& bo, int size, double idle_s) {
    auto r = xrt::run(k);
    r.set_arg(pl::S2MM_ARG_MEM, bo);
    r.set_arg(pl::S2MM_ARG_SIZE, size);
    r.set_arg(pl::S2MM_ARG_IDLE, idle_s > 0 ? idle_cycles(idle_s) : 0);
    r.start();
    return r;
}
static uint32_t s2mm_count(xrt::bo& bo, int size) {
    uint32_t n = 0;
    bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, 4, (size_t)size * 4);
    bo.read(&n, 4, (size_t)size * 4);
    return std::min<uint32_t>(n, (uint32_t)size);
}

// Drop whatever a previous (killed / aborted) session left in the output FIFOs.
// Called with the RF input parked (iq_mux: playback + drain RF) and before the
// epoch bump; a capture left half-way in ofdm_fe is handled by the pre-roll.
static void purge(Xrt& x) {
    const int W = 16384;
    size_t tot = 0;
    for (auto* k : {&x.s2mm_csi, &x.s2mm_feat}) {
        auto bo = xrt::bo(x.dev, (size_t)(W + 1) * 4, k->group_id(0));
        for (int it = 0; it < 64; it++) {
            auto r = s2mm_start(*k, bo, W, 0.05);
            r.wait();
            const uint32_t n = s2mm_count(bo, W);
            tot += n;
            if (n < (uint32_t)W) break;
        }
    }
    if (tot) std::fprintf(stderr, "purge: dropped %zu stale output words of a previous session\n", tot);
}

// Continuous drain of fixed-size records from one s2mm CU. Every run returns after
// up to 8 records or 20 ms without data (no run is ever left pending); the words
// are re-framed on the record magic, so a record split across runs is harmless.
static void drain_loop(xrt::device& dev, xrt::kernel& k, int words, std::atomic<bool>& go,
                       int every, bool is_csi) {
    const int batch = 8 * words;
    auto bo = xrt::bo(dev, (size_t)(batch + 1) * 4, k.group_id(0));
    std::vector<uint32_t> buf(batch), pend;
    const uint32_t magic = is_csi ? pl::REC_MAGIC : pl::TAG_MAGIC;
    uint64_t n = 0;
    while (go) {
        auto run = s2mm_start(k, bo, batch, 0.02);
        run.wait();
        const uint32_t got = s2mm_count(bo, batch);
        if (!got) continue;
        bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, (size_t)got * 4, 0);
        bo.read(buf.data(), (size_t)got * 4, 0);
        pend.insert(pend.end(), buf.begin(), buf.begin() + got);
        size_t i = 0, skipped = 0;
        while (pend.size() - i >= (size_t)words) {
            if (pend[i] != magic) { i++; skipped++; continue; }
            if (is_csi) {
                if (every > 0 && (n++ % (uint64_t)every) == 0) emit(csi_line(&pend[i]));
            } else {
                emit(feat_line(&pend[i]));
            }
            i += (size_t)words;
        }
        pend.erase(pend.begin(), pend.begin() + (std::ptrdiff_t)i);
        if (skipped) emit(std::string("ERR,") + (is_csi ? "csi" : "feature") + " stream re-framed, skipped " +
                          std::to_string(skipped) + " words");
    }
}

static std::atomic<bool> g_stop{false};
static void on_signal(int) { g_stop = true; }

// ------------------------------------------------------------------ modes --
static int cmd_selftest(const std::string& xclbin, const std::string& vd) {
    // zero pre-roll (pl_map.h restart protocol) + vectors + flush (as golden_chain)
    auto iq = std::vector<uint32_t>((size_t)pl::PREROLL_WORDS, 0u);
    { auto d = read_words(vd + "/iq80.bin"); iq.insert(iq.end(), d.begin(), d.end()); }
    auto gcsi = read_words(vd + "/csi_rec.bin");
    auto gfeat = read_words(vd + "/feat.bin");
    if (iq.size() <= (size_t)pl::PREROLL_WORDS || gcsi.empty()) {
        std::fprintf(stderr, "need %s/{iq80,csi_rec,feat}.bin\n", vd.c_str());
        return 2;
    }
    for (int i = 0; i < pl::FLUSH_WORDS; i++) iq.push_back(0);
    while (iq.size() % 4) iq.push_back(0);
    const int ncsi = (int)(gcsi.size() / pl::REC_WORDS), nfeat = (int)(gfeat.size() / pl::FEAT_WORDS);
    RxCfg c;
    c.nco_inc = (uint32_t)json_num(vd + "/vectors.json", "nco_inc", 0);
    c.feat_ctrl = pl::FEAT_PKT_BINS | pl::FEAT_SIG_ONLY;
    c.fs_nom = (float)json_num(vd + "/vectors.json", "pkt_rate", 20.0);
    Xrt x(xclbin);
    Pl p;
    if (!p.open_()) return 3;
    p.w(pl::IQ_MUX + pl::iq_mux::SEL, pl::SEL_PLAYBACK | pl::SEL_DRAIN_RF);
    purge(x);
    configure(p, c);
    std::fprintf(stderr, "selftest: %zu IQ words, expecting %d CSI records + %d feature records (nco_inc=%u)\n",
                 iq.size(), ncsi, nfeat, c.nco_inc);

    // receive buffers with slack (+64 records): extra records are reported, never
    // back-pressure the pipeline; both runs end 1 s after their stream went quiet.
    const int csi_cap = (int)gcsi.size() + 64 * pl::REC_WORDS, feat_cap = (int)gfeat.size() + 64 * pl::FEAT_WORDS;
    auto bo_iq = xrt::bo(x.dev, iq.size() * 4, x.mm2s.group_id(0));
    auto bo_csi = xrt::bo(x.dev, (size_t)(csi_cap + 1) * 4, x.s2mm_csi.group_id(0));
    auto bo_feat = xrt::bo(x.dev, (size_t)(feat_cap + 1) * 4, x.s2mm_feat.group_id(0));
    bo_iq.write(iq.data());
    bo_iq.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    auto r_csi = s2mm_start(x.s2mm_csi, bo_csi, csi_cap, 1.0);
    auto r_feat = s2mm_start(x.s2mm_feat, bo_feat, feat_cap, 1.0);
    const auto t0 = std::chrono::steady_clock::now();
    auto r_iq = xrt::run(x.mm2s);
    r_iq.set_arg(0, bo_iq); r_iq.set_arg(2, (int)iq.size()); r_iq.start();
    const bool ok_iq = r_iq.wait(60s) == ERT_CMD_STATE_COMPLETED;
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const bool ok_csi = r_csi.wait(30s) == ERT_CMD_STATE_COMPLETED;
    const bool ok_feat = r_feat.wait(30s) == ERT_CMD_STATE_COMPLETED;
    const uint32_t n_csi = ok_csi ? s2mm_count(bo_csi, csi_cap) : 0, n_feat = ok_feat ? s2mm_count(bo_feat, feat_cap) : 0;
    print_status(p);
    std::fprintf(stderr, "runs: mm2s %s (%.3f s, %.1f MSPS playback)  s2mm_csi %s %u words  s2mm_feat %s %u words\n",
                 ok_iq ? "done" : "TIMEOUT", dt, iq.size() / dt / 1e6, ok_csi ? "done" : "TIMEOUT", n_csi,
                 ok_feat ? "done" : "TIMEOUT", n_feat);
    int rc = (ok_iq && ok_csi && ok_feat) ? 0 : 1;
    if (n_csi != gcsi.size() || n_feat != gfeat.size()) {
        std::printf("record count: CSI %u/%zu words, features %u/%zu words -> FAIL\n", n_csi, gcsi.size(), n_feat,
                    gfeat.size());
        rc = 1;
    }

    std::vector<uint32_t> hc(gcsi.size()), hf(gfeat.size());
    bo_csi.sync(XCL_BO_SYNC_BO_FROM_DEVICE, gcsi.size() * 4, 0); bo_csi.read(hc.data(), gcsi.size() * 4, 0);
    size_t bad = 0; double maxrel = 0.0;
    for (size_t i = 0; i < gcsi.size(); i++) {
        if (hc[i] == gcsi[i]) continue;
        bad++;
        if ((i % pl::REC_WORDS) >= (size_t)pl::REC_HDR) {
            const double a = u2f(hc[i]), g = u2f(gcsi[i]);
            maxrel = std::max(maxrel, std::fabs(a - g) / (std::fabs(g) + 1e-9));
        }
    }
    std::printf("CSI records : %d x %d words, %zu words differ from golden (max rel CSI diff %.2e) -> %s\n",
                ncsi, pl::REC_WORDS, bad, maxrel, bad == 0 ? "PASS (bit-exact)" : (maxrel < 1e-5 ? "PASS" : "FAIL"));
    if (bad && maxrel >= 1e-5) rc = 1;
    if (nfeat > 0) {
        bo_feat.sync(XCL_BO_SYNC_BO_FROM_DEVICE, gfeat.size() * 4, 0); bo_feat.read(hf.data(), gfeat.size() * 4, 0);
        size_t fbad = 0; double ferr = 0.0;
        for (size_t i = 0; i < gfeat.size(); i++) {
            const size_t w = i % pl::FEAT_WORDS;
            if (w < 8 || w == (size_t)pl::FEAT_WORDS - 1) { if (hf[i] != gfeat[i]) fbad++; continue; }
            const double a = u2f(hf[i]), g = u2f(gfeat[i]);
            const double e = std::fabs(a - g) / (std::fabs(g) + 1e-6);
            ferr = std::max(ferr, e);
            if (e > 1e-3) fbad++;
        }
        std::printf("AIE features: %d x %d words, %zu out of tolerance (max rel err %.2e) -> %s\n",
                    nfeat, pl::FEAT_WORDS, fbad, ferr, fbad == 0 ? "PASS" : "FAIL");
        if (fbad) rc = 1;
        for (int k = 0; k < nfeat; k += std::max(1, nfeat / 8)) emit(feat_line(&hf[(size_t)k * pl::FEAT_WORDS]));
    }
    std::printf("SELFTEST %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}

static int cmd_stream(const std::string& xclbin, const std::string& vd, bool live, int argc, char** argv) {
    bool loop = false;
    double speed = 1.0;
    int csi_every = 4, chan = 6;
    RxCfg c;
    for (int i = 0; i < argc; i++) {
        const std::string a = argv[i];
        auto nx = [&](double d) { return (i + 1 < argc) ? std::atof(argv[++i]) : d; };
        if (a == "--loop") loop = true;
        else if (a == "--speed") speed = nx(1.0);
        else if (a == "--csi-every") csi_every = (int)nx(4);
        else if (a == "--chan") chan = (int)nx(6);
        else if (a == "--bin-us") c.bin_us = (uint32_t)nx(50000);
        else if (a == "--thr") c.thr2 = (float)std::pow(nx(0.7), 2.0);
        else if (a == "--pmin") c.p_min = (float)nx(0);
        else if (a == "--gain") c.ddc_ctrl = ((uint32_t)nx(0) & 7u) << 8;
    }
    Xrt x(xclbin);
    Pl p;
    if (!p.open_()) return 3;
    std::vector<uint32_t> iq;
    std::vector<size_t> cuts;
    if (live) {
        if (chan < 1 || chan > 11) { std::fprintf(stderr, "2.4 GHz channel 1..11 (RF-DC NCO at ch6)\n"); return 2; }
        const double f_off = (2407.0 + 5.0 * chan - 2437.0) * 1e6;
        c.nco_inc = (uint32_t)(int64_t)std::llround(-f_off / 80e6 * 4294967296.0);
        c.feat_ctrl = pl::FEAT_SIG_ONLY;                        // time bins @ bin_us
        c.fs_nom = (float)(1e6 / c.bin_us);
        p.w(pl::IQ_MUX + pl::iq_mux::SEL, pl::SEL_PLAYBACK | pl::SEL_DRAIN_RF);
        purge(x);
        p.w(pl::IQ_MUX + pl::iq_mux::SEL, 0);                  // RF, fabric MMCM held in reset
        std::this_thread::sleep_for(100ms);
        p.w(pl::IQ_MUX + pl::iq_mux::SEL, pl::SEL_RF_RESETN);  // release: MMCM locks on clk_adc0
        std::fprintf(stderr, "live: channel %d (%.0f MHz, PL NCO offset %+.0f MHz)\n", chan, 2407.0 + 5.0 * chan, f_off / 1e6);
    } else {
        iq = read_words(vd + "/iq80.bin");
        if (iq.empty()) { std::fprintf(stderr, "no %s/iq80.bin\n", vd.c_str()); return 2; }
        iq.insert(iq.begin(), (size_t)pl::PREROLL_WORDS, 0u);   // restart pre-roll (pl_map.h)
        c.nco_inc = (uint32_t)json_num(vd + "/vectors.json", "nco_inc", 0);
        c.feat_ctrl = pl::FEAT_PKT_BINS | pl::FEAT_SIG_ONLY;
        c.fs_nom = (float)json_num(vd + "/vectors.json", "pkt_rate", 20.0);
        std::ifstream cf(vd + "/chunks.txt");
        cuts.push_back(0);
        for (size_t w; cf >> w;) if (w) cuts.push_back(w + (size_t)pl::PREROLL_WORDS);
        if (cuts.size() == 1) for (size_t w = 1u << 16; w < iq.size(); w += 1u << 16) cuts.push_back(w);
        cuts.push_back(iq.size());
        p.w(pl::IQ_MUX + pl::iq_mux::SEL, pl::SEL_PLAYBACK | pl::SEL_DRAIN_RF);
        purge(x);
    }
    configure(p, c);
    std::atomic<bool> go{true};
    std::thread tc([&] { drain_loop(x.dev, x.s2mm_csi, pl::REC_WORDS, go, csi_every, true); });
    std::thread tf([&] { drain_loop(x.dev, x.s2mm_feat, pl::FEAT_WORDS, go, 1, false); });
    std::thread ts([&] { while (go) { print_status(p); std::this_thread::sleep_for(1s); } });

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    auto finish = [&] {
        go = false;                                              // runs end within 20 ms
        tc.join(); tf.join(); ts.join();
        if (live) p.w(pl::IQ_MUX + pl::iq_mux::SEL, pl::SEL_PLAYBACK | pl::SEL_DRAIN_RF);
        return 0;
    };
    if (live) {
        while (!g_stop) std::this_thread::sleep_for(200ms);     // until SIGINT / SIGTERM
        return finish();
    }
    // paced playback: cut k (every 20 packets, chunks.txt) is due at k * 1 s / speed.
    // Each chunk is copied into one staging buffer (no sub-buffer alignment rules);
    // a packet split across two runs is harmless (the kernels just see a pause).
    size_t maxlen = 4096;
    for (size_t k = 0; k + 1 < cuts.size(); k++) maxlen = std::max(maxlen, cuts[k + 1] - cuts[k]);
    auto bo = xrt::bo(x.dev, maxlen * 4, x.mm2s.group_id(0));
    const double pkt_rate = json_num(vd + "/vectors.json", "pkt_rate", 20.0);
    const double chunk_s = 20.0 / pkt_rate / std::max(speed, 1e-3);
    auto play = [&](const uint32_t* d, size_t len) {
        bo.write(d, len * 4, 0);
        bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, len * 4, 0);
        auto r = xrt::run(x.mm2s);
        r.set_arg(0, bo); r.set_arg(2, (int)len); r.start();
        r.wait();
    };
    do {
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t k = 0; k + 1 < cuts.size() && !g_stop; k++) {
            play(iq.data() + cuts[k], cuts[k + 1] - cuts[k]);
            std::this_thread::sleep_until(t0 + std::chrono::duration<double>((k + 1) * chunk_s));
        }
    } while (loop && !g_stop);
    std::vector<uint32_t> zeros((size_t)pl::FLUSH_WORDS, 0);     // flush the last packet out
    play(zeros.data(), zeros.size());
    std::this_thread::sleep_for(1s);
    return finish();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s selftest <xclbin> <vecdir> | play <xclbin> <vecdir> [--loop] [--speed X] "
                             "[--csi-every N] | live <xclbin> [--chan N] [--bin-us U] [--thr T] [--pmin P] [--gain G] "
                             "| status\n", argv[0]);
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "selftest" && argc >= 4) return cmd_selftest(argv[2], argv[3]);
        if (cmd == "play" && argc >= 4) return cmd_stream(argv[2], argv[3], false, argc - 4, argv + 4);
        if (cmd == "live" && argc >= 3) return cmd_stream(argv[2], "", true, argc - 3, argv + 3);
        if (cmd == "status") {
            Pl p;
            if (!p.open_()) return 3;
            print_status(p);
            std::printf("iq_mux.sel=0x%x wifi_ddc.nco=0x%08x ofdm_be.last_sig=0x%08x fe.thr2=%.3f\n",
                        p.r(pl::IQ_MUX + pl::iq_mux::SEL), p.r(pl::WIFI_DDC + pl::ddc::NCO_INC),
                        p.r(pl::OFDM_BE + pl::be::LAST_SIG), u2f(p.r(pl::OFDM_FE + pl::fe::THR2)));
            return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "host_wifi: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "bad arguments\n");
    return 2;
}
