// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xcsi_feat.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XCsi_feat_CfgInitialize(XCsi_feat *InstancePtr, XCsi_feat_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XCsi_feat_Set_ctrl(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_CTRL_DATA, Data);
}

u32 XCsi_feat_Get_ctrl(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_CTRL_DATA);
    return Data;
}

void XCsi_feat_Set_bin_us(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_BIN_US_DATA, Data);
}

u32 XCsi_feat_Get_bin_us(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_BIN_US_DATA);
    return Data;
}

void XCsi_feat_Set_hop(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_HOP_DATA, Data);
}

u32 XCsi_feat_Get_hop(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_HOP_DATA);
    return Data;
}

void XCsi_feat_Set_br_dec(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_BR_DEC_DATA, Data);
}

u32 XCsi_feat_Get_br_dec(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_BR_DEC_DATA);
    return Data;
}

void XCsi_feat_Set_k_ref(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_K_REF_DATA, Data);
}

u32 XCsi_feat_Get_k_ref(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_K_REF_DATA);
    return Data;
}

void XCsi_feat_Set_fs_nom(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_FS_NOM_DATA, Data);
}

u32 XCsi_feat_Get_fs_nom(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_FS_NOM_DATA);
    return Data;
}

void XCsi_feat_Set_alpha(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_ALPHA_DATA, Data);
}

u32 XCsi_feat_Get_alpha(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_ALPHA_DATA);
    return Data;
}

void XCsi_feat_Set_epoch(XCsi_feat *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCsi_feat_WriteReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_EPOCH_DATA, Data);
}

u32 XCsi_feat_Get_epoch(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_EPOCH_DATA);
    return Data;
}

u32 XCsi_feat_Get_n_in(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_N_IN_DATA);
    return Data;
}

u32 XCsi_feat_Get_n_win(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_N_WIN_DATA);
    return Data;
}

u32 XCsi_feat_Get_n_samp(XCsi_feat *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCsi_feat_ReadReg(InstancePtr->Control_BaseAddress, XCSI_FEAT_CONTROL_ADDR_N_SAMP_DATA);
    return Data;
}

