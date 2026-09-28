#pragma once
// ---------------------------------------------------------------------------
// wifi_common.h - shared definitions for the VRK160 WiFi-CSI PL pipeline:
//
//   RF-DC (6.4 GSPS, DDC /2 /40) --80 MSPS IQ--> iq_mux --> wifi_ddc (NCO + /4)
//   --20 MSPS--> ofdm_fe (detect/capture) --> ofdm_be (CFO, FFT, CSI, L-SIG)
//   --CSI record--> csi_feat --float windows--> AIE feature graph --> feat_pack
//   --feature record--> s2mm_feat --> DDR ;  ofdm_be rec --> s2mm_csi --> DDR
//
// Every kernel is written so the SAME source is (a) v++-linked for the XRT
// design, (b) exported as a Vivado IP for the plain design (hw/ip_repo), and
// (c) compiled with g++ as the bit-exact golden model (hls/tb).
//
// Sample word  : {Q[31:16], I[15:0]}, int16 two's complement (RF-DC format).
// All records are streams of 32-bit words, TLAST on the last word.
// ---------------------------------------------------------------------------
#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>
#include <stdint.h>

typedef ap_axiu<32, 0, 0, 0> axis32_t;

// ---- ofdm_fe -> ofdm_be capture ----------------------------------------------
static const int STF_DLY  = 16;    // L-STF period
static const int STF_WIN  = 48;    // autocorrelation window
static const int CAP_PRE  = 32;    // capture starts CAP_PRE samples before trigger
static const int N_CAP    = 416;   // captured samples per packet
static const int T1_LO    = 40;    // L-LTF T1 search range (capture coordinates)
static const int T1_HI    = 200;
static const int CAP_TRL  = 8;     // trailer words after the samples
static const int CAP_WORDS = N_CAP + CAP_TRL;
static const uint32_t CAP_MAGIC = 0xCA970001u;
static const uint32_t CAP_STALE = 0x80000000u;   // trailer seq flag: completed while armed
// Host zero pre-roll (80 MSPS words) played after every epoch change: long enough for
// a capture that was in flight in ofdm_fe to complete (as CAP_STALE) before new data.
static const int PREROLL_WORDS = 2048;
static_assert(PREROLL_WORDS >= 4 * (CAP_WORDS + 8), "pre-roll must outlast one capture");
// trailer: [0]=magic [1]=t1 [2]=C.re(f32) [3]=C.im(f32) [4]=P(f32)
//          [5]=ts_lo  [6]=ts_hi (20 MSPS sample index of capture word 0) [7]=seq

// ---- CSI record (ofdm_be -> csi_feat, and -> s2mm_csi -> DDR) -----------------
static const int REC_HDR   = 16;
static const int REC_WORDS = REC_HDR + 2 * 64;          // 144 words = 576 B
static const uint32_t REC_MAGIC = 0xC51F0001u;
enum RecWord {
    RW_MAGIC = 0, RW_SEQ = 1, RW_TS_LO = 2, RW_TS_HI = 3,
    RW_SIG = 4,        // [23:0] decoded L-SIG bits, [24] sig_ok, [25] parity_ok, [26] tail_ok
    RW_CFO = 5,        // f32 Hz
    RW_RSSI = 6,       // f32 dBFS of the L-STF (mean |x|^2 re full scale)
    RW_SNR = 7,        // f32 dB (L-LTF T1/T2 difference noise estimate)
    RW_T1 = 8,         // int  T1 index in capture coordinates
    RW_METRIC = 9,     // f32 |C|/P at trigger
    RW_NOISE = 10,     // f32 noise power per subcarrier
    RW_SIGPWR = 11,    // f32 mean |H|^2 over used subcarriers
    RW_FMT = 12,       // [7:0]=64 subcarriers, [15:8]=0 float32 re/im, [23:16]=rate Mbps
    RW_LEN = 13,       // L-SIG LENGTH (bytes)
    RW_RSV0 = 14, RW_RSV1 = 15
};
// words 16..143: H[k].re, H[k].im (f32) for FFT bins k = 0..63 (nulls = 0)

// ---- csi_feat -> feat_pack tag, feat_pack -> DDR feature record ---------------
static const int WIN_MOT = 256;    // AIE motion  branch (params.h BLOCK)
static const int WIN_BRT = 64;     // AIE breathing branch (dftmat.h N)
static const int WIN_PHS = 256;    // AIE phase-var branch (params.h L)
static const int OUT_MOT = 3, OUT_BRT = 33, OUT_PHS = 3;
static const int TAG_WORDS = 12;
static const uint32_t TAG_MAGIC = 0xFEA70001u;
// tag: [0]=magic [1]=win_seq [2]=ts_lo [3]=ts_hi [4]=n_pkts (in this hop, bits 23:0;
//      bit 31 = breathing window history full)
//      [5]=rssi_avg f32 [6]=snr_avg f32 [7]=fs_csi f32 [8]=sum(mot window) f32
//      [9]=sum(brt window) f32 [10]=sum(phs window) f32 [11]=cfo_avg f32
static const int FEAT_WORDS = TAG_WORDS + OUT_MOT + OUT_BRT + OUT_PHS + 1;  // 52 words = 208 B

// ---- PL control address map (identical in the v++ and the plain Vivado design)
#define PL_IQ_MUX_BASE   0xA5000000u
#define PL_WIFI_DDC_BASE 0xA5010000u
#define PL_OFDM_FE_BASE  0xA5020000u
#define PL_OFDM_BE_BASE  0xA5030000u
#define PL_CSI_FEAT_BASE 0xA5040000u
#define PL_RFDC_BASE     0xA5200000u

static inline float u2f(uint32_t u) { union { uint32_t u; float f; } c; c.u = u; return c.f; }
static inline uint32_t f2u(float f) { union { uint32_t u; float f; } c; c.f = f; return c.u; }

static inline axis32_t mkword(uint32_t d, bool last) {
    axis32_t w;
    w.data = d;
    w.keep = -1;
    w.strb = -1;
    w.last = last ? 1 : 0;
    return w;
}
