// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
// control
// 0x00 : reserved
// 0x04 : reserved
// 0x08 : reserved
// 0x0c : reserved
// 0x10 : Data signal of sel
//        bit 31~0 - sel[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of rf_count
//        bit 31~0 - rf_count[31:0] (Read)
// 0x1c : Control signal of rf_count
//        bit 0  - rf_count_ap_vld (Read/COR)
//        others - reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XIQ_MUX_CONTROL_ADDR_SEL_DATA      0x10
#define XIQ_MUX_CONTROL_BITS_SEL_DATA      32
#define XIQ_MUX_CONTROL_ADDR_RF_COUNT_DATA 0x18
#define XIQ_MUX_CONTROL_BITS_RF_COUNT_DATA 32
#define XIQ_MUX_CONTROL_ADDR_RF_COUNT_CTRL 0x1c

