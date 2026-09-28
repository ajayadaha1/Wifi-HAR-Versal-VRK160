// pl_map.h - host-side view of the VRK160 WiFi-HAR PL: control addresses,
// HLS register offsets and record layouts. Plain C++ (no HLS headers) so it
// cross-compiles with the PetaLinux SDK. Source of truth:
//   hls/common/wifi_common.h (records, base addresses)
//   hw/ip_repo/<k>/drivers/*/src/x<k>_hw.h (register offsets, HLS-generated)
#pragma once
#include <cstdint>

namespace pl {
// ---- control apertures (pinned by aie/overlay_rf.tcl and the plain design) ----
constexpr uint64_t BASE      = 0xA5000000ull;   // one 4 MB /dev/mem window
constexpr uint64_t SPAN      = 0x00400000ull;
constexpr uint32_t IQ_MUX    = 0x000000;
constexpr uint32_t WIFI_DDC  = 0x010000;
constexpr uint32_t OFDM_FE   = 0x020000;
constexpr uint32_t OFDM_BE   = 0x030000;
constexpr uint32_t CSI_FEAT  = 0x040000;
constexpr uint32_t RFDC      = 0x200000;       // Versal RF data converter (2 MB)

// ---- HLS s_axilite register offsets -------------------------------------------
namespace iq_mux  { constexpr uint32_t SEL = 0x10, RF_COUNT = 0x18; }
namespace ddc     { constexpr uint32_t NCO_INC = 0x10, CTRL = 0x18, EPOCH = 0x20; }
namespace fe      { constexpr uint32_t THR2 = 0x10, P_MIN = 0x18, N_PLAT = 0x20, HOLDOFF = 0x28,
                                       EPOCH = 0x30, N_TRIG = 0x38, P_NOW = 0x48; }
namespace be      { constexpr uint32_t CTRL = 0x10, EPOCH = 0x18, N_PKT = 0x20, N_SIG_OK = 0x30,
                                       LAST_SIG = 0x40, N_RESYNC = 0x50; }
namespace feat    { constexpr uint32_t CTRL = 0x10, BIN_US = 0x18, HOP = 0x20, BR_DEC = 0x28,
                                       K_REF = 0x30, FS_NOM = 0x38, ALPHA = 0x40, EPOCH = 0x48,
                                       N_IN = 0x50, N_WIN = 0x60, N_SAMP = 0x70; }
// iq_mux.SEL bits
constexpr uint32_t SEL_PLAYBACK = 1u << 0, SEL_DRAIN_RF = 1u << 1, SEL_RF_RESETN = 1u << 2;
// ofdm_be.CTRL bits
constexpr uint32_t BE_REC_EN = 1u << 0, BE_REC_SIG_ONLY = 1u << 1;
// csi_feat.CTRL bits
constexpr uint32_t FEAT_PKT_BINS = 1u << 0, FEAT_SIG_ONLY = 1u << 1;

// ---- records (32-bit words) -------------------------------------------------
constexpr int REC_HDR = 16, REC_WORDS = 144;     // CSI record (s2mm_csi)
constexpr uint32_t REC_MAGIC = 0xC51F0001u;
enum RecWord { RW_MAGIC = 0, RW_SEQ, RW_TS_LO, RW_TS_HI, RW_SIG, RW_CFO, RW_RSSI, RW_SNR, RW_T1,
               RW_METRIC, RW_NOISE, RW_SIGPWR, RW_FMT, RW_LEN, RW_FESEQ, RW_RSV };
constexpr int TAG_WORDS = 12, OUT_MOT = 3, OUT_BRT = 33, OUT_PHS = 3;
constexpr int FEAT_WORDS = TAG_WORDS + OUT_MOT + OUT_BRT + OUT_PHS + 1;   // 52 (s2mm_feat)
constexpr uint32_t TAG_MAGIC = 0xFEA70001u;
constexpr double FS20 = 20e6;                    // OFDM sample rate (timestamps)

// ---- restart protocol (wifi_common.h) -----------------------------------------
// After every epoch bump the playback starts with PREROLL_WORDS zero words: the
// kernels are only ARMED by the epoch and restart at the first non-zero sample; a
// capture still in flight in ofdm_fe completes inside the pre-roll (flagged
// CAP_STALE, dropped by ofdm_be), so a restarted pipeline is bit-identical to a
// freshly configured one.
constexpr int PREROLL_WORDS = 2048;
constexpr int FLUSH_WORDS = 4096;                // trailing zeros: push the last packet out
// ---- data movers (XRT arg indices) ----------------------------------------------
// mm2s(mem, s, size); s2mm(mem, s, size, idle_max): s2mm also returns after idle_max
// idle clock cycles (0 = blocking) and writes the received word count to mem[size].
constexpr int S2MM_ARG_MEM = 0, S2MM_ARG_SIZE = 2, S2MM_ARG_IDLE = 3;
constexpr double PL_CLK_HZ = 312.5e6;            // kernel clock (idle_max unit)
}  // namespace pl
