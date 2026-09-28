// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xiq_mux.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XIq_mux_CfgInitialize(XIq_mux *InstancePtr, XIq_mux_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XIq_mux_Set_sel(XIq_mux *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XIq_mux_WriteReg(InstancePtr->Control_BaseAddress, XIQ_MUX_CONTROL_ADDR_SEL_DATA, Data);
}

u32 XIq_mux_Get_sel(XIq_mux *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XIq_mux_ReadReg(InstancePtr->Control_BaseAddress, XIQ_MUX_CONTROL_ADDR_SEL_DATA);
    return Data;
}

u32 XIq_mux_Get_rf_count(XIq_mux *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XIq_mux_ReadReg(InstancePtr->Control_BaseAddress, XIQ_MUX_CONTROL_ADDR_RF_COUNT_DATA);
    return Data;
}

u32 XIq_mux_Get_rf_count_vld(XIq_mux *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XIq_mux_ReadReg(InstancePtr->Control_BaseAddress, XIQ_MUX_CONTROL_ADDR_RF_COUNT_CTRL);
    return Data & 0x1;
}

