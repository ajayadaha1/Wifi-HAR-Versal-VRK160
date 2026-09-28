// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xofdm_be.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XOfdm_be_CfgInitialize(XOfdm_be *InstancePtr, XOfdm_be_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XOfdm_be_Set_ctrl(XOfdm_be *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XOfdm_be_WriteReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_CTRL_DATA, Data);
}

u32 XOfdm_be_Get_ctrl(XOfdm_be *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_be_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_CTRL_DATA);
    return Data;
}

void XOfdm_be_Set_epoch(XOfdm_be *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XOfdm_be_WriteReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_EPOCH_DATA, Data);
}

u32 XOfdm_be_Get_epoch(XOfdm_be *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_be_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_EPOCH_DATA);
    return Data;
}

u32 XOfdm_be_Get_n_pkt(XOfdm_be *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_be_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_N_PKT_DATA);
    return Data;
}

u32 XOfdm_be_Get_n_sig_ok(XOfdm_be *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_be_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_N_SIG_OK_DATA);
    return Data;
}

u32 XOfdm_be_Get_last_sig(XOfdm_be *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_be_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_LAST_SIG_DATA);
    return Data;
}

u32 XOfdm_be_Get_n_resync(XOfdm_be *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_be_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_BE_CONTROL_ADDR_N_RESYNC_DATA);
    return Data;
}

