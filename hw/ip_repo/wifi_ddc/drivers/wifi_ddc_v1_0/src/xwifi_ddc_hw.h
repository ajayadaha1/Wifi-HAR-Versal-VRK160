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
// 0x10 : Data signal of nco_inc
//        bit 31~0 - nco_inc[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of ctrl
//        bit 31~0 - ctrl[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of epoch
//        bit 31~0 - epoch[31:0] (Read/Write)
// 0x24 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XWIFI_DDC_CONTROL_ADDR_NCO_INC_DATA 0x10
#define XWIFI_DDC_CONTROL_BITS_NCO_INC_DATA 32
#define XWIFI_DDC_CONTROL_ADDR_CTRL_DATA    0x18
#define XWIFI_DDC_CONTROL_BITS_CTRL_DATA    32
#define XWIFI_DDC_CONTROL_ADDR_EPOCH_DATA   0x20
#define XWIFI_DDC_CONTROL_BITS_EPOCH_DATA   32

