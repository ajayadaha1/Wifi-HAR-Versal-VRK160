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
#include "xcsi_feat.h"

extern XCsi_feat_Config XCsi_feat_ConfigTable[];

#ifdef SDT
XCsi_feat_Config *XCsi_feat_LookupConfig(UINTPTR BaseAddress) {
	XCsi_feat_Config *ConfigPtr = NULL;

	int Index;

	for (Index = (u32)0x0; XCsi_feat_ConfigTable[Index].Name != NULL; Index++) {
		if (!BaseAddress || XCsi_feat_ConfigTable[Index].Control_BaseAddress == BaseAddress) {
			ConfigPtr = &XCsi_feat_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XCsi_feat_Initialize(XCsi_feat *InstancePtr, UINTPTR BaseAddress) {
	XCsi_feat_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XCsi_feat_LookupConfig(BaseAddress);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XCsi_feat_CfgInitialize(InstancePtr, ConfigPtr);
}
#else
XCsi_feat_Config *XCsi_feat_LookupConfig(u16 DeviceId) {
	XCsi_feat_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XCSI_FEAT_NUM_INSTANCES; Index++) {
		if (XCsi_feat_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XCsi_feat_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XCsi_feat_Initialize(XCsi_feat *InstancePtr, u16 DeviceId) {
	XCsi_feat_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XCsi_feat_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XCsi_feat_CfgInitialize(InstancePtr, ConfigPtr);
}
#endif

#endif

