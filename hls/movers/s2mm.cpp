// PL data mover (XRT kernel): AXI4-Stream -> DDR (AXI4-MM), up to `size` 32-bit words.
//   s2mm_csi  : CSI records (REC_WORDS each) from ofdm_be/rec
//   s2mm_feat : feature records (FEAT_WORDS each) from feat_pack
// idle_max = 0 : blocking, returns after exactly `size` words (VCK190 behaviour).
// idle_max > 0 : also returns after idle_max consecutive clock cycles without data,
//                so no run is ever left pending (host exit, restart purge, timeouts).
// Either way mem[size] = number of words received: the host allocates size + 1
// words and never has to guess whether a record arrived. TLAST is ignored
// (records are fixed-size; the host re-frames on the record magic).
#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

extern "C" void s2mm(ap_uint<32>* mem, hls::stream<ap_axiu<32, 0, 0, 0> >& s, int size, int idle_max) {
#pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem max_write_burst_length=64 num_write_outstanding=16 depth=4096
#pragma HLS INTERFACE s_axilite port=mem bundle=control
#pragma HLS INTERFACE s_axilite port=size bundle=control
#pragma HLS INTERFACE s_axilite port=idle_max bundle=control
#pragma HLS INTERFACE axis port=s
#pragma HLS INTERFACE s_axilite port=return bundle=control
    int n = 0, idle = 0;
rx:
    while (n < size) {
#pragma HLS PIPELINE II=1
        ap_axiu<32, 0, 0, 0> v;
        if (s.read_nb(v)) {
            mem[n] = v.data;
            n++;
            idle = 0;
        } else if (idle_max > 0) {
            if (++idle >= idle_max) break;
        }
    }
    mem[size] = n;
}
