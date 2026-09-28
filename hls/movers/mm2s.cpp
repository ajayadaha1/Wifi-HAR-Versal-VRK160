// PL data mover (XRT kernel): DDR (AXI4-MM) -> AXI4-Stream, `size` 32-bit words.
//   mm2s_iq : IQ playback into iq_mux/pb -> wifi_ddc (self-test / demo without RF)
// TLAST on the last word. Same mover as the VCK190 design (aie/pl/mm2s.cpp).
#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

extern "C" void mm2s(const ap_uint<32>* mem, hls::stream<ap_axiu<32, 0, 0, 0> >& s, int size) {
#pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem max_read_burst_length=64 num_read_outstanding=16 depth=4096
#pragma HLS INTERFACE s_axilite port=mem bundle=control
#pragma HLS INTERFACE s_axilite port=size bundle=control
#pragma HLS INTERFACE axis port=s
#pragma HLS INTERFACE s_axilite port=return bundle=control
    for (int i = 0; i < size; i++) {
#pragma HLS PIPELINE II=1
        ap_axiu<32, 0, 0, 0> v;
        v.data = mem[i];
        v.keep = -1;
        v.strb = -1;
        v.last = (i == size - 1) ? 1 : 0;
        s.write(v);
    }
}
