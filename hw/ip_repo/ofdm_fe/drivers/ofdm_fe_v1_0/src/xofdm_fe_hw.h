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
// 0x10 : Data signal of thr2
//        bit 31~0 - thr2[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of p_min
//        bit 31~0 - p_min[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of n_plat
//        bit 31~0 - n_plat[31:0] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of holdoff
//        bit 31~0 - holdoff[31:0] (Read/Write)
// 0x2c : reserved
// 0x30 : Data signal of epoch
//        bit 31~0 - epoch[31:0] (Read/Write)
// 0x34 : reserved
// 0x38 : Data signal of n_trig
//        bit 31~0 - n_trig[31:0] (Read)
// 0x3c : reserved
// 0x48 : Data signal of p_now
//        bit 31~0 - p_now[31:0] (Read)
// 0x4c : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XOFDM_FE_CONTROL_ADDR_THR2_DATA    0x10
#define XOFDM_FE_CONTROL_BITS_THR2_DATA    32
#define XOFDM_FE_CONTROL_ADDR_P_MIN_DATA   0x18
#define XOFDM_FE_CONTROL_BITS_P_MIN_DATA   32
#define XOFDM_FE_CONTROL_ADDR_N_PLAT_DATA  0x20
#define XOFDM_FE_CONTROL_BITS_N_PLAT_DATA  32
#define XOFDM_FE_CONTROL_ADDR_HOLDOFF_DATA 0x28
#define XOFDM_FE_CONTROL_BITS_HOLDOFF_DATA 32
#define XOFDM_FE_CONTROL_ADDR_EPOCH_DATA   0x30
#define XOFDM_FE_CONTROL_BITS_EPOCH_DATA   32
#define XOFDM_FE_CONTROL_ADDR_N_TRIG_DATA  0x38
#define XOFDM_FE_CONTROL_BITS_N_TRIG_DATA  32
#define XOFDM_FE_CONTROL_ADDR_P_NOW_DATA   0x48
#define XOFDM_FE_CONTROL_BITS_P_NOW_DATA   32

