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
#include "xiq_mux.h"

extern XIq_mux_Config XIq_mux_ConfigTable[];

#ifdef SDT
XIq_mux_Config *XIq_mux_LookupConfig(UINTPTR BaseAddress) {
	XIq_mux_Config *ConfigPtr = NULL;

	int Index;

	for (Index = (u32)0x0; XIq_mux_ConfigTable[Index].Name != NULL; Index++) {
		if (!BaseAddress || XIq_mux_ConfigTable[Index].Control_BaseAddress == BaseAddress) {
			ConfigPtr = &XIq_mux_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XIq_mux_Initialize(XIq_mux *InstancePtr, UINTPTR BaseAddress) {
	XIq_mux_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XIq_mux_LookupConfig(BaseAddress);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XIq_mux_CfgInitialize(InstancePtr, ConfigPtr);
}
#else
XIq_mux_Config *XIq_mux_LookupConfig(u16 DeviceId) {
	XIq_mux_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XIQ_MUX_NUM_INSTANCES; Index++) {
		if (XIq_mux_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XIq_mux_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XIq_mux_Initialize(XIq_mux *InstancePtr, u16 DeviceId) {
	XIq_mux_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XIq_mux_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XIq_mux_CfgInitialize(InstancePtr, ConfigPtr);
}
#endif

#endif

