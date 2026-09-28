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
// 0x18 : Data signal of bin_us
//        bit 31~0 - bin_us[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of hop
//        bit 31~0 - hop[31:0] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of br_dec
//        bit 31~0 - br_dec[31:0] (Read/Write)
// 0x2c : reserved
// 0x30 : Data signal of k_ref
//        bit 31~0 - k_ref[31:0] (Read/Write)
// 0x34 : reserved
// 0x38 : Data signal of fs_nom
//        bit 31~0 - fs_nom[31:0] (Read/Write)
// 0x3c : reserved
// 0x40 : Data signal of alpha
//        bit 31~0 - alpha[31:0] (Read/Write)
// 0x44 : reserved
// 0x48 : Data signal of epoch
//        bit 31~0 - epoch[31:0] (Read/Write)
// 0x4c : reserved
// 0x50 : Data signal of n_in
//        bit 31~0 - n_in[31:0] (Read)
// 0x54 : reserved
// 0x60 : Data signal of n_win
//        bit 31~0 - n_win[31:0] (Read)
// 0x64 : reserved
// 0x70 : Data signal of n_samp
//        bit 31~0 - n_samp[31:0] (Read)
// 0x74 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XCSI_FEAT_CONTROL_ADDR_CTRL_DATA   0x10
#define XCSI_FEAT_CONTROL_BITS_CTRL_DATA   32
#define XCSI_FEAT_CONTROL_ADDR_BIN_US_DATA 0x18
#define XCSI_FEAT_CONTROL_BITS_BIN_US_DATA 32
#define XCSI_FEAT_CONTROL_ADDR_HOP_DATA    0x20
#define XCSI_FEAT_CONTROL_BITS_HOP_DATA    32
#define XCSI_FEAT_CONTROL_ADDR_BR_DEC_DATA 0x28
#define XCSI_FEAT_CONTROL_BITS_BR_DEC_DATA 32
#define XCSI_FEAT_CONTROL_ADDR_K_REF_DATA  0x30
#define XCSI_FEAT_CONTROL_BITS_K_REF_DATA  32
#define XCSI_FEAT_CONTROL_ADDR_FS_NOM_DATA 0x38
#define XCSI_FEAT_CONTROL_BITS_FS_NOM_DATA 32
#define XCSI_FEAT_CONTROL_ADDR_ALPHA_DATA  0x40
#define XCSI_FEAT_CONTROL_BITS_ALPHA_DATA  32
#define XCSI_FEAT_CONTROL_ADDR_EPOCH_DATA  0x48
#define XCSI_FEAT_CONTROL_BITS_EPOCH_DATA  32
#define XCSI_FEAT_CONTROL_ADDR_N_IN_DATA   0x50
#define XCSI_FEAT_CONTROL_BITS_N_IN_DATA   32
#define XCSI_FEAT_CONTROL_ADDR_N_WIN_DATA  0x60
#define XCSI_FEAT_CONTROL_BITS_N_WIN_DATA  32
#define XCSI_FEAT_CONTROL_ADDR_N_SAMP_DATA 0x70
#define XCSI_FEAT_CONTROL_BITS_N_SAMP_DATA 32

