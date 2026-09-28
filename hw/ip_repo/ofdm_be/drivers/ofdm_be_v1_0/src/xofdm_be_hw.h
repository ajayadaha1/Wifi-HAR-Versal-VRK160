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
// 0x10 : Data signal of ctrl
//        bit 31~0 - ctrl[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of epoch
//        bit 31~0 - epoch[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of n_pkt
//        bit 31~0 - n_pkt[31:0] (Read)
// 0x24 : reserved
// 0x30 : Data signal of n_sig_ok
//        bit 31~0 - n_sig_ok[31:0] (Read)
// 0x34 : reserved
// 0x40 : Data signal of last_sig
//        bit 31~0 - last_sig[31:0] (Read)
// 0x44 : reserved
// 0x50 : Data signal of n_resync
//        bit 31~0 - n_resync[31:0] (Read)
// 0x54 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XOFDM_BE_CONTROL_ADDR_CTRL_DATA     0x10
#define XOFDM_BE_CONTROL_BITS_CTRL_DATA     32
#define XOFDM_BE_CONTROL_ADDR_EPOCH_DATA    0x18
#define XOFDM_BE_CONTROL_BITS_EPOCH_DATA    32
#define XOFDM_BE_CONTROL_ADDR_N_PKT_DATA    0x20
#define XOFDM_BE_CONTROL_BITS_N_PKT_DATA    32
#define XOFDM_BE_CONTROL_ADDR_N_SIG_OK_DATA 0x30
#define XOFDM_BE_CONTROL_BITS_N_SIG_OK_DATA 32
#define XOFDM_BE_CONTROL_ADDR_LAST_SIG_DATA 0x40
#define XOFDM_BE_CONTROL_BITS_LAST_SIG_DATA 32
#define XOFDM_BE_CONTROL_ADDR_N_RESYNC_DATA 0x50
#define XOFDM_BE_CONTROL_BITS_N_RESYNC_DATA 32

