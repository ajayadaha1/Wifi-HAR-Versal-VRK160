// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XOFDM_FE_H
#define XOFDM_FE_H

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
#include "xofdm_fe_hw.h"

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
} XOfdm_fe_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XOfdm_fe;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XOfdm_fe_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XOfdm_fe_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XOfdm_fe_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XOfdm_fe_ReadReg(BaseAddress, RegOffset) \
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
int XOfdm_fe_Initialize(XOfdm_fe *InstancePtr, UINTPTR BaseAddress);
XOfdm_fe_Config* XOfdm_fe_LookupConfig(UINTPTR BaseAddress);
#else
int XOfdm_fe_Initialize(XOfdm_fe *InstancePtr, u16 DeviceId);
XOfdm_fe_Config* XOfdm_fe_LookupConfig(u16 DeviceId);
#endif
int XOfdm_fe_CfgInitialize(XOfdm_fe *InstancePtr, XOfdm_fe_Config *ConfigPtr);
#else
int XOfdm_fe_Initialize(XOfdm_fe *InstancePtr, const char* InstanceName);
int XOfdm_fe_Release(XOfdm_fe *InstancePtr);
#endif


void XOfdm_fe_Set_thr2(XOfdm_fe *InstancePtr, u32 Data);
u32 XOfdm_fe_Get_thr2(XOfdm_fe *InstancePtr);
void XOfdm_fe_Set_p_min(XOfdm_fe *InstancePtr, u32 Data);
u32 XOfdm_fe_Get_p_min(XOfdm_fe *InstancePtr);
void XOfdm_fe_Set_n_plat(XOfdm_fe *InstancePtr, u32 Data);
u32 XOfdm_fe_Get_n_plat(XOfdm_fe *InstancePtr);
void XOfdm_fe_Set_holdoff(XOfdm_fe *InstancePtr, u32 Data);
u32 XOfdm_fe_Get_holdoff(XOfdm_fe *InstancePtr);
void XOfdm_fe_Set_epoch(XOfdm_fe *InstancePtr, u32 Data);
u32 XOfdm_fe_Get_epoch(XOfdm_fe *InstancePtr);
u32 XOfdm_fe_Get_n_trig(XOfdm_fe *InstancePtr);
u32 XOfdm_fe_Get_p_now(XOfdm_fe *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
