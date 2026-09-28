# Build the bit-exact software golden model of the VRK160 PL+AIE chain.
# Needs only g++ and the Vitis HLS headers (no license, no board).
#
#   make -C hls/tb                       # -> hls/tb/golden_chain
#   make -C hls/tb check                 # scenario -> golden -> python cross-check
XILINX_HLS ?= /proj/gsd/vivado/2025.2/Vitis
PY         ?= python3
CXX        ?= g++
CXXFLAGS   := -O2 -std=c++14 -I$(XILINX_HLS)/include -Wno-unknown-pragmas -Wno-unused-label \
              -Wno-unused-variable -Wno-unused-but-set-variable -Wno-int-in-bool-context
SRCS       := golden_chain.cpp $(wildcard ../common/*.h ../wifi_ddc/*.cpp ../ofdm_rx/*.cpp ../csi_feat/*.cpp) \
              $(wildcard ../../aie/src/*_core.h)
OUT        ?= $(abspath ../../build/vectors/check)

golden_chain: $(SRCS)
	$(CXX) $(CXXFLAGS) golden_chain.cpp -o $@

check: golden_chain
	mkdir -p $(OUT)
	$(PY) ../../csi_ref/make_vectors.py --out $(OUT) --preset quick
	./golden_chain $(OUT)/iq80.bin $(OUT) $$(cat $(OUT)/golden_args.txt)
	$(PY) ../../csi_ref/check_golden.py $(OUT)

clean:
	rm -f golden_chain

.PHONY: check clean
