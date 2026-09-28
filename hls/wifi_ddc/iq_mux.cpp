// ---------------------------------------------------------------------------
// iq_mux.cpp - selects the IQ source for the WiFi receiver (overlay IP, not an
// XRT kernel: it sits on the mm2s_iq -> wifi_ddc net that v++ creates).
//   rf : RF-DC m00_axis (via the async CDC FIFO) - TDATA/TVALID/TREADY only
//   pb : DDR playback from the XRT mm2s kernel (mm2s_iq)
// Registers (s_axilite, PL_IQ_MUX_BASE):
//   sel[0]   0 -> rf (live), 1 -> pb (playback / self-test)
//   sel[1]   1 -> keep draining the unselected rf input (discarded)
//   sel[2]   RF clock-domain reset release (drives rf_resetn): write 1 once the
//            RF-DC tile is up so the 100->80 MHz fabric MMCM locks on a stable clock
//   rf_count (status) RF samples received so far -> "is the ADC streaming?"
// Free-running (ap_ctrl_none), one sample per clock.
// ---------------------------------------------------------------------------
#include "../common/wifi_common.h"

void iq_mux(hls::stream<ap_uint<32> >& rf, hls::stream<axis32_t>& pb,
            hls::stream<axis32_t>& iq_out, ap_uint<32> sel,
            ap_uint<1>* rf_resetn, ap_uint<32>* rf_count) {
#pragma HLS INTERFACE mode=axis port=rf
#pragma HLS INTERFACE mode=axis port=pb
#pragma HLS INTERFACE mode=axis port=iq_out
#pragma HLS INTERFACE mode=s_axilite port=sel bundle=control
#pragma HLS INTERFACE mode=ap_none port=rf_resetn
#pragma HLS INTERFACE mode=s_axilite port=rf_count bundle=control
#pragma HLS INTERFACE mode=ap_ctrl_none port=return
#pragma HLS PIPELINE II=1
    static ap_uint<32> cnt = 0;
    *rf_resetn = sel[2];
    if (sel[0]) {
        axis32_t v;
        if (pb.read_nb(v)) iq_out.write(mkword(v.data, v.last));
        ap_uint<32> d;
        if (sel[1] && rf.read_nb(d)) cnt++;
    } else {
        ap_uint<32> d;
        if (rf.read_nb(d)) {
            iq_out.write(mkword(d, false));
            cnt++;
        }
    }
    *rf_count = cnt;
}
