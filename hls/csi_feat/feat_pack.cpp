// ---------------------------------------------------------------------------
// feat_pack.cpp - joins one csi_feat tag with the three AI Engine branch
// outputs of the same window into one FEAT_WORDS feature record for s2mm_feat:
//   [0..11] tag  [12..14] motion {mean,var,power}  [15..47] breathing |X| bins
//   [48..50] phase-var {mean,var,power}  [51] 0         (TLAST on word 51)
// AIE PLIO outputs are read by count (TLAST is not relied upon). Branches are
// consumed strictly in window order, so the ratio is always 1:1:1:1.
// Free-running, no registers.
// ---------------------------------------------------------------------------
#include "../common/wifi_common.h"

void feat_pack(hls::stream<axis32_t>& tag, hls::stream<axis32_t>& mot, hls::stream<axis32_t>& brt,
               hls::stream<axis32_t>& phs, hls::stream<axis32_t>& feat_out) {
#pragma HLS INTERFACE mode=axis port=tag
#pragma HLS INTERFACE mode=axis port=mot
#pragma HLS INTERFACE mode=axis port=brt
#pragma HLS INTERFACE mode=axis port=phs
#pragma HLS INTERFACE mode=axis port=feat_out
#pragma HLS INTERFACE mode=ap_ctrl_none port=return
    uint32_t buf[FEAT_WORDS];
    bool aligned = true;
rd_tag:
    for (int i = 0; i < TAG_WORDS; i++) {
#pragma HLS PIPELINE II=1
        axis32_t w = tag.read();
        buf[i] = (uint32_t)w.data;
        if ((i == TAG_WORDS - 1) != (w.last == 1)) aligned = false;
    }
rd_mot:
    for (int i = 0; i < OUT_MOT; i++) {
#pragma HLS PIPELINE II=1
        buf[TAG_WORDS + i] = (uint32_t)mot.read().data;
    }
rd_brt:
    for (int i = 0; i < OUT_BRT; i++) {
#pragma HLS PIPELINE II=1
        buf[TAG_WORDS + OUT_MOT + i] = (uint32_t)brt.read().data;
    }
rd_phs:
    for (int i = 0; i < OUT_PHS; i++) {
#pragma HLS PIPELINE II=1
        buf[TAG_WORDS + OUT_MOT + OUT_BRT + i] = (uint32_t)phs.read().data;
    }
    buf[FEAT_WORDS - 1] = aligned ? 0u : 1u;          // 1 = tag framing error seen
wr:
    for (int i = 0; i < FEAT_WORDS; i++) {
#pragma HLS PIPELINE II=1
        feat_out.write(mkword(buf[i], i == FEAT_WORDS - 1));
    }
}
