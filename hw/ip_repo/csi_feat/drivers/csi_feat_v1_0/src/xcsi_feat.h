// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XCSI_FEAT_H
#define XCSI_FEAT_H

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
#include "xcsi_feat_hw.h"

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
} XCsi_feat_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XCsi_feat;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XCsi_feat_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XCsi_feat_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XCsi_feat_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XCsi_feat_ReadReg(BaseAddress, RegOffset) \
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
int XCsi_feat_Initialize(XCsi_feat *InstancePtr, UINTPTR BaseAddress);
XCsi_feat_Config* XCsi_feat_LookupConfig(UINTPTR BaseAddress);
#else
int XCsi_feat_Initialize(XCsi_feat *InstancePtr, u16 DeviceId);
XCsi_feat_Config* XCsi_feat_LookupConfig(u16 DeviceId);
#endif
int XCsi_feat_CfgInitialize(XCsi_feat *InstancePtr, XCsi_feat_Config *ConfigPtr);
#else
int XCsi_feat_Initialize(XCsi_feat *InstancePtr, const char* InstanceName);
int XCsi_feat_Release(XCsi_feat *InstancePtr);
#endif


void XCsi_feat_Set_ctrl(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_ctrl(XCsi_feat *InstancePtr);
void XCsi_feat_Set_bin_us(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_bin_us(XCsi_feat *InstancePtr);
void XCsi_feat_Set_hop(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_hop(XCsi_feat *InstancePtr);
void XCsi_feat_Set_br_dec(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_br_dec(XCsi_feat *InstancePtr);
void XCsi_feat_Set_k_ref(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_k_ref(XCsi_feat *InstancePtr);
void XCsi_feat_Set_fs_nom(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_fs_nom(XCsi_feat *InstancePtr);
void XCsi_feat_Set_alpha(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_alpha(XCsi_feat *InstancePtr);
void XCsi_feat_Set_epoch(XCsi_feat *InstancePtr, u32 Data);
u32 XCsi_feat_Get_epoch(XCsi_feat *InstancePtr);
u32 XCsi_feat_Get_n_in(XCsi_feat *InstancePtr);
u32 XCsi_feat_Get_n_win(XCsi_feat *InstancePtr);
u32 XCsi_feat_Get_n_samp(XCsi_feat *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
