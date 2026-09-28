// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#ifdef SDT
#include "xparameters.h"
#endif
#include "xwifi_ddc.h"

extern XWifi_ddc_Config XWifi_ddc_ConfigTable[];

#ifdef SDT
XWifi_ddc_Config *XWifi_ddc_LookupConfig(UINTPTR BaseAddress) {
	XWifi_ddc_Config *ConfigPtr = NULL;

	int Index;

	for (Index = (u32)0x0; XWifi_ddc_ConfigTable[Index].Name != NULL; Index++) {
		if (!BaseAddress || XWifi_ddc_ConfigTable[Index].Control_BaseAddress == BaseAddress) {
			ConfigPtr = &XWifi_ddc_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XWifi_ddc_Initialize(XWifi_ddc *InstancePtr, UINTPTR BaseAddress) {
	XWifi_ddc_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XWifi_ddc_LookupConfig(BaseAddress);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XWifi_ddc_CfgInitialize(InstancePtr, ConfigPtr);
}
#else
XWifi_ddc_Config *XWifi_ddc_LookupConfig(u16 DeviceId) {
	XWifi_ddc_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XWIFI_DDC_NUM_INSTANCES; Index++) {
		if (XWifi_ddc_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XWifi_ddc_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XWifi_ddc_Initialize(XWifi_ddc *InstancePtr, u16 DeviceId) {
	XWifi_ddc_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XWifi_ddc_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XWifi_ddc_CfgInitialize(InstancePtr, ConfigPtr);
}
#endif

#endif

