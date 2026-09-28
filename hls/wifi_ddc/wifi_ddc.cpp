// ---------------------------------------------------------------------------
// wifi_ddc.cpp - 80 MSPS -> 20 MSPS WiFi channel selector.
//
// The RF-DC already mixes the RF carrier to baseband (fine NCO) and decimates
// 6.4 GSPS -> 80 MSPS. wifi_ddc adds a PL NCO so any 2.4 GHz channel within
// +/-30 MHz of the RF-DC NCO (ch1..ch11 around ch6) is selectable at run time,
// then a 64-tap symmetric FIR (0-8.6 MHz pass, >=11.4 MHz stop, ddc_taps.h)
// decimates by 4 to the 802.11 20 MSPS rate.
//
// Registers (s_axilite, PL_WIFI_DDC_BASE):
//   nco_inc : phase increment per 80 MSPS sample; wifi_ofdm.nco_inc_for(f_off)
//             moves a signal at +f_off Hz to DC (y = x * exp(+j*2*pi*phase/2^32))
//   ctrl    : [0] swap I/Q  [1] conjugate (spectral flip)  [2] bypass NCO
//             [11:8] output gain, left shift 0..7
//   epoch   : a change ARMS a restart; NCO phase, delay line and decimation phase
//             reset at the first non-zero input sample (output forced to 0 while armed)
// Free-running (ap_ctrl_none), II = 1: one input sample per call, one output
// sample every 4th call.
// ---------------------------------------------------------------------------
#include "../common/wifi_common.h"
#include "../common/ddc_taps.h"
#include "../common/wifi_tables.h"

static inline ap_int<16> sat16(ap_int<50> v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (ap_int<16>)v;
}

void wifi_ddc(hls::stream<axis32_t>& iq_in, hls::stream<axis32_t>& iq_out,
              ap_uint<32> nco_inc, ap_uint<32> ctrl, ap_uint<32> epoch) {
#pragma HLS INTERFACE mode=axis port=iq_in
#pragma HLS INTERFACE mode=axis port=iq_out
#pragma HLS INTERFACE mode=s_axilite port=nco_inc bundle=control
#pragma HLS INTERFACE mode=s_axilite port=ctrl bundle=control
#pragma HLS INTERFACE mode=s_axilite port=epoch bundle=control
#ifdef HLS_COSIM_HS
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control
#else
#pragma HLS INTERFACE mode=ap_ctrl_none port=return
#endif
#pragma HLS PIPELINE II=1
    static ap_uint<32> phase = 0;
    static ap_int<16> dli[DDC_NTAPS];
    static ap_int<16> dlq[DDC_NTAPS];
#pragma HLS ARRAY_PARTITION variable=dli complete
#pragma HLS ARRAY_PARTITION variable=dlq complete
    static ap_uint<2> dph = 0;          // decimation phase: output after sample dph == 3
    static bool grp_last = false;
    static bool armed = false;
    static ap_uint<32> last_epoch = 0;

    // Deterministic restart: a new `epoch` only ARMS; the reset (NCO phase, delay
    // line, decimation phase) happens in-band at the first non-zero input sample.
    // While armed the output is forced to 0, so downstream kernels see a clean
    // zero pre-roll and restart at the same sample in hardware and in the golden
    // model, whatever the pipeline was doing when the register was written.
    if (epoch != last_epoch) { last_epoch = epoch; armed = true; }
    const axis32_t w = iq_in.read();
    if (armed && w.data != 0) {
        armed = false;
        phase = 0;
        dph = 0;
        grp_last = false;
        for (int k = 0; k < DDC_NTAPS; k++) { dli[k] = 0; dlq[k] = 0; }
    }

    const bool swap = ctrl[0], conj = ctrl[1], byp = ctrl[2];
    const int gsh = (int)ctrl(10, 8);
    ap_int<16> xi = (ap_int<16>)w.data(15, 0);
    ap_int<16> xq = (ap_int<16>)w.data(31, 16);
    if (swap) { ap_int<16> t = xi; xi = xq; xq = t; }
    if (conj) xq = (xq == -32768) ? (ap_int<16>)32767 : (ap_int<16>)(-xq);
    ap_int<16> yi = xi, yq = xq;
    if (!byp) {
        ap_uint<10> idx = phase(31, 22);
        ap_int<16> c = COS1K[idx];
        ap_int<16> sn = COS1K[(ap_uint<10>)(idx - 256)];
        ap_int<34> ri = (ap_int<34>)xi * c - (ap_int<34>)xq * sn;
        ap_int<34> rq = (ap_int<34>)xi * sn + (ap_int<34>)xq * c;
        yi = sat16((ri + (1 << 14)) >> 15);
        yq = sat16((rq + (1 << 14)) >> 15);
    }
    phase += nco_inc;
    for (int k = DDC_NTAPS - 1; k > 0; k--) { dli[k] = dli[k - 1]; dlq[k] = dlq[k - 1]; }
    dli[0] = yi;
    dlq[0] = yq;
    const bool last = grp_last || (w.last == 1);

    if (dph == 3) {
        ap_int<50> ai = 0, aq = 0;
        for (int k = 0; k < DDC_NTAPS / 2; k++) {
#pragma HLS UNROLL
            ap_int<18> h = DDC_TAPS[k];
            ai += (ap_int<50>)h * (ap_int<17>)(dli[k] + dli[DDC_NTAPS - 1 - k]);
            aq += (ap_int<50>)h * (ap_int<17>)(dlq[k] + dlq[DDC_NTAPS - 1 - k]);
        }
        const int sh = DDC_QBITS - gsh;
        const ap_int<50> rnd = (ap_int<50>)1 << (sh - 1);
        ap_int<16> oi = sat16((ai + rnd) >> sh);
        ap_int<16> oq = sat16((aq + rnd) >> sh);
        ap_uint<32> d = 0;
        if (!armed) {
            d(15, 0) = (ap_uint<16>)oi;
            d(31, 16) = (ap_uint<16>)oq;
        }
        iq_out.write(mkword(d, last));
        grp_last = false;
    } else {
        grp_last = last;
    }
    dph++;
}
