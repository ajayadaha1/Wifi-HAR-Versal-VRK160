// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xofdm_fe.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XOfdm_fe_CfgInitialize(XOfdm_fe *InstancePtr, XOfdm_fe_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XOfdm_fe_Set_thr2(XOfdm_fe *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XOfdm_fe_WriteReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_THR2_DATA, Data);
}

u32 XOfdm_fe_Get_thr2(XOfdm_fe *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_fe_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_THR2_DATA);
    return Data;
}

void XOfdm_fe_Set_p_min(XOfdm_fe *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XOfdm_fe_WriteReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_P_MIN_DATA, Data);
}

u32 XOfdm_fe_Get_p_min(XOfdm_fe *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_fe_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_P_MIN_DATA);
    return Data;
}

void XOfdm_fe_Set_n_plat(XOfdm_fe *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XOfdm_fe_WriteReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_N_PLAT_DATA, Data);
}

u32 XOfdm_fe_Get_n_plat(XOfdm_fe *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_fe_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_N_PLAT_DATA);
    return Data;
}

void XOfdm_fe_Set_holdoff(XOfdm_fe *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XOfdm_fe_WriteReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_HOLDOFF_DATA, Data);
}

u32 XOfdm_fe_Get_holdoff(XOfdm_fe *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_fe_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_HOLDOFF_DATA);
    return Data;
}

void XOfdm_fe_Set_epoch(XOfdm_fe *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XOfdm_fe_WriteReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_EPOCH_DATA, Data);
}

u32 XOfdm_fe_Get_epoch(XOfdm_fe *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_fe_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_EPOCH_DATA);
    return Data;
}

u32 XOfdm_fe_Get_n_trig(XOfdm_fe *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_fe_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_N_TRIG_DATA);
    return Data;
}

u32 XOfdm_fe_Get_p_now(XOfdm_fe *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XOfdm_fe_ReadReg(InstancePtr->Control_BaseAddress, XOFDM_FE_CONTROL_ADDR_P_NOW_DATA);
    return Data;
}

