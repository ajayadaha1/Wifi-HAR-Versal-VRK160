// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XOFDM_BE_H
#define XOFDM_BE_H

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
#include "xofdm_be_hw.h"

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
} XOfdm_be_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XOfdm_be;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XOfdm_be_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XOfdm_be_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XOfdm_be_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XOfdm_be_ReadReg(BaseAddress, RegOffset) \
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
int XOfdm_be_Initialize(XOfdm_be *InstancePtr, UINTPTR BaseAddress);
XOfdm_be_Config* XOfdm_be_LookupConfig(UINTPTR BaseAddress);
#else
int XOfdm_be_Initialize(XOfdm_be *InstancePtr, u16 DeviceId);
XOfdm_be_Config* XOfdm_be_LookupConfig(u16 DeviceId);
#endif
int XOfdm_be_CfgInitialize(XOfdm_be *InstancePtr, XOfdm_be_Config *ConfigPtr);
#else
int XOfdm_be_Initialize(XOfdm_be *InstancePtr, const char* InstanceName);
int XOfdm_be_Release(XOfdm_be *InstancePtr);
#endif


void XOfdm_be_Set_ctrl(XOfdm_be *InstancePtr, u32 Data);
u32 XOfdm_be_Get_ctrl(XOfdm_be *InstancePtr);
void XOfdm_be_Set_epoch(XOfdm_be *InstancePtr, u32 Data);
u32 XOfdm_be_Get_epoch(XOfdm_be *InstancePtr);
u32 XOfdm_be_Get_n_pkt(XOfdm_be *InstancePtr);
u32 XOfdm_be_Get_n_sig_ok(XOfdm_be *InstancePtr);
u32 XOfdm_be_Get_last_sig(XOfdm_be *InstancePtr);
u32 XOfdm_be_Get_n_resync(XOfdm_be *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
