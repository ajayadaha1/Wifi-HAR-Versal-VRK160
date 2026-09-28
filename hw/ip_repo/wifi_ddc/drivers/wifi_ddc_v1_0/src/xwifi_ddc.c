// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xwifi_ddc.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XWifi_ddc_CfgInitialize(XWifi_ddc *InstancePtr, XWifi_ddc_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XWifi_ddc_Set_nco_inc(XWifi_ddc *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XWifi_ddc_WriteReg(InstancePtr->Control_BaseAddress, XWIFI_DDC_CONTROL_ADDR_NCO_INC_DATA, Data);
}

u32 XWifi_ddc_Get_nco_inc(XWifi_ddc *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XWifi_ddc_ReadReg(InstancePtr->Control_BaseAddress, XWIFI_DDC_CONTROL_ADDR_NCO_INC_DATA);
    return Data;
}

void XWifi_ddc_Set_ctrl(XWifi_ddc *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XWifi_ddc_WriteReg(InstancePtr->Control_BaseAddress, XWIFI_DDC_CONTROL_ADDR_CTRL_DATA, Data);
}

u32 XWifi_ddc_Get_ctrl(XWifi_ddc *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XWifi_ddc_ReadReg(InstancePtr->Control_BaseAddress, XWIFI_DDC_CONTROL_ADDR_CTRL_DATA);
    return Data;
}

void XWifi_ddc_Set_epoch(XWifi_ddc *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XWifi_ddc_WriteReg(InstancePtr->Control_BaseAddress, XWIFI_DDC_CONTROL_ADDR_EPOCH_DATA, Data);
}

u32 XWifi_ddc_Get_epoch(XWifi_ddc *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XWifi_ddc_ReadReg(InstancePtr->Control_BaseAddress, XWIFI_DDC_CONTROL_ADDR_EPOCH_DATA);
    return Data;
}

