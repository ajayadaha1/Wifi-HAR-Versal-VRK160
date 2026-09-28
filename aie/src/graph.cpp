#pragma once
#include <adf.h>
#include "fir.h"
#include "stats.h"
#include "dft.h"
using namespace adf;

// Three-branch CSI feature graph on the VR1602 AI Engine array (AIE1, 42x3).
// Kernels are the silicon-proven VCK190 pure-C cores, unchanged; only the
// device/platform and the PLIO clock hint differ. The inputs are produced in
// the PL by csi_feat (hls/csi_feat) from the OFDM receiver's per-packet CSI:
//   motion:    PLIO_in(BLOCK=256)   normalised mean |H| @ fs_csi
//              -> FIR band-pass (1-3 Hz) -> stats -> PLIO_out {mean,var,power}
//   breathing: PLIO_brt_in(N=64)    same signal decimated to fs_csi/BR_DEC
//              -> Hann-windowed DFT magnitude -> PLIO_brt_out (NB=33 bins)
//   phase-var: PLIO_phase_in(L=256) linear-sanitised CSI phase @ fs_csi
//              -> stats -> PLIO_phase_out {mean, variance(=phase-var), power}
static constexpr double PL_MHZ = 312.5;   // vrk160_base default kernel clock (index 1)

class FeatureGraph : public graph {
public:
    kernel kfir, kstats, kdft, kphase;
    input_plio in, brt_in, phase_in;
    output_plio out, brt_out, phase_out;

    FeatureGraph() {
        kfir = kernel::create(fir);
        kstats = kernel::create(stats);
        kdft = kernel::create(dft_mag);
        kphase = kernel::create(stats);
        in = input_plio::create("PLIO_in", plio_32_bits, "data/input.txt", PL_MHZ);
        out = output_plio::create("PLIO_out", plio_32_bits, "data/output.txt", PL_MHZ);
        brt_in = input_plio::create("PLIO_brt_in", plio_32_bits, "data/breath_input.txt", PL_MHZ);
        brt_out = output_plio::create("PLIO_brt_out", plio_32_bits, "data/breath_output.txt", PL_MHZ);
        phase_in = input_plio::create("PLIO_phase_in", plio_32_bits, "data/phase_input.txt", PL_MHZ);
        phase_out = output_plio::create("PLIO_phase_out", plio_32_bits, "data/phase_output.txt", PL_MHZ);

        connect<>(in.out[0], kfir.in[0]);
        connect<>(kfir.out[0], kstats.in[0]);
        connect<>(kstats.out[0], out.in[0]);
        connect<>(brt_in.out[0], kdft.in[0]);
        connect<>(kdft.out[0], brt_out.in[0]);
        connect<>(phase_in.out[0], kphase.in[0]);
        connect<>(kphase.out[0], phase_out.in[0]);

        dimensions(kfir.in[0]) = {BLOCK};
        dimensions(kfir.out[0]) = {BLOCK};
        dimensions(kstats.in[0]) = {L};
        dimensions(kstats.out[0]) = {NOUT};
        dimensions(kdft.in[0]) = {N};
        dimensions(kdft.out[0]) = {NB};
        dimensions(kphase.in[0]) = {L};
        dimensions(kphase.out[0]) = {NOUT};

        source(kfir) = "fir.cpp";
        source(kstats) = "stats.cpp";
        source(kdft) = "dft.cpp";
        source(kphase) = "stats.cpp";
        runtime<ratio>(kfir) = 0.5;
        runtime<ratio>(kstats) = 0.5;
        runtime<ratio>(kdft) = 0.5;
        runtime<ratio>(kphase) = 0.5;
    }
};

FeatureGraph feature_graph;

#if defined(__X86SIM__) || defined(__AIESIM__)
int main(void) {
    feature_graph.init();
    feature_graph.run(1);
    feature_graph.end();
    return 0;
}
#endif
