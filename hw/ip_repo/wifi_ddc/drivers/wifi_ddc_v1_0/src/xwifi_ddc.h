// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XWIFI_DDC_H
#define XWIFI_DDC_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#ifndef __linux__
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xwifi_ddc_hw.h"

/**************************** Type Definitions ******************************/
#ifdef __linux__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef struct {
#ifdef SDT
    char *Name;
#else
    u16 DeviceId;
#endif
    u64 Control_BaseAddress;
} XWifi_ddc_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XWifi_ddc;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XWifi_ddc_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XWifi_ddc_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XWifi_ddc_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XWifi_ddc_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#ifndef __linux__
#ifdef SDT
int XWifi_ddc_Initialize(XWifi_ddc *InstancePtr, UINTPTR BaseAddress);
XWifi_ddc_Config* XWifi_ddc_LookupConfig(UINTPTR BaseAddress);
#else
int XWifi_ddc_Initialize(XWifi_ddc *InstancePtr, u16 DeviceId);
XWifi_ddc_Config* XWifi_ddc_LookupConfig(u16 DeviceId);
#endif
int XWifi_ddc_CfgInitialize(XWifi_ddc *InstancePtr, XWifi_ddc_Config *ConfigPtr);
#else
int XWifi_ddc_Initialize(XWifi_ddc *InstancePtr, const char* InstanceName);
int XWifi_ddc_Release(XWifi_ddc *InstancePtr);
#endif


void XWifi_ddc_Set_nco_inc(XWifi_ddc *InstancePtr, u32 Data);
u32 XWifi_ddc_Get_nco_inc(XWifi_ddc *InstancePtr);
void XWifi_ddc_Set_ctrl(XWifi_ddc *InstancePtr, u32 Data);
u32 XWifi_ddc_Get_ctrl(XWifi_ddc *InstancePtr);
void XWifi_ddc_Set_epoch(XWifi_ddc *InstancePtr, u32 Data);
u32 XWifi_ddc_Get_epoch(XWifi_ddc *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
