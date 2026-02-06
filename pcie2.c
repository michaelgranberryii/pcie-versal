/******************************************************************************
* Copyright (C) 2019 - 2022 Xilinx, Inc.  All rights reserved.
* Copyright (C) 2022 - 2023 Advanced Micro Devices, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
*******************************************************************************/

/******************************************************************************/
/**
* @file xdmapcie_rc_enumerate_two_rp_example.c
*
* Example showing how to use TWO XDMA/QDMA PCIe Root Ports (Root Complex ports)
* and enumerate the PCIe fabric behind each port.
*
* What this example does:
*  - Initializes Root Port 0, waits for link-up, programs RC header (CMD + bus nums)
*  - Enumerates fabric behind Root Port 0
*  - Initializes Root Port 1, waits for link-up, programs RC header (CMD + bus nums)
*  - Enumerates fabric behind Root Port 1
*
* IMPORTANT:
*  - You MUST assign NON-OVERLAPPING bus number ranges per root port.
*  - Your Vivado design / address map must also ensure non-overlapping MMIO windows
*    if BAR assignment is performed automatically by the driver.
*
******************************************************************************/

/***************************** Include Files ********************************/
#include "xparameters.h"   /* XPAR constants */
#include "xdmapcie.h"      /* XDmaPcie driver */
#include "stdio.h"
#include "xil_printf.h"
#include "sleep.h"

/************************** Constant Definitions ****************************/

/* Parameters for the waiting for link up routine */
#define XDMAPCIE_LINK_WAIT_MAX_RETRIES   10
#define XDMAPCIE_LINK_WAIT_USLEEP_MIN    90000

/*
 * Command register bits (PCI/PCIe command register)
 */
#define PCIE_CFG_CMD_IO_EN       0x00000001 /* I/O access enable */
#define PCIE_CFG_CMD_MEM_EN      0x00000002 /* Memory access enable */
#define PCIE_CFG_CMD_BUSM_EN     0x00000004 /* Bus master enable */
#define PCIE_CFG_CMD_PARITY      0x00000040 /* parity errors response */
#define PCIE_CFG_CMD_SERR_EN     0x00000100 /* SERR report enable */

/*
 * PCIe Configuration registers offsets (WORD offsets in this driver API)
 * (These match your original example style.)
 */
#define PCIE_CFG_CMD_STATUS_REG      0x0001 /* Command/Status */
#define PCIE_CFG_PRI_SEC_BUS_REG     0x0006 /* Primary/Secondary/Subordinate bus */

/*
 * BUS NUMBERING PLAN (example)
 * We give each Root Port its own downstream bus range.
 *
 * Root Port 0:
 *   Primary = 0x00
 *   Secondary = 0x01
 *   Subordinate = 0x7F
 *
 * Root Port 1:
 *   Primary = 0x00
 *   Secondary = 0x80
 *   Subordinate = 0xFF
 *
 * NOTE: The packing of Primary/Secondary/Subordinate in the 32-bit value
 * depends on the register layout. The original example used 0x00070100 to
 * represent prim=0, sec=1, sub=7 (conceptually).
 *
 * We keep the SAME packing style as the original example:
 *   value = (Subordinate << 16) | (Secondary << 8) | (Primary << 0)
 */
#define PCIE_CFG_MAKE_BUSNUM(prim, sec, sub)   ( ((u32)(sub) << 16) | ((u32)(sec) << 8) | ((u32)(prim) << 0) )

#define RP0_PRIM_BUS   0x00
#define RP0_SEC_BUS    0x01
#define RP0_SUB_BUS    0x7F

#define RP1_PRIM_BUS   0x00
#define RP1_SEC_BUS    0x80
#define RP1_SUB_BUS    0xFF

#define RP0_BUSNUM_VALUE   PCIE_CFG_MAKE_BUSNUM(RP0_PRIM_BUS, RP0_SEC_BUS, RP0_SUB_BUS)
#define RP1_BUSNUM_VALUE   PCIE_CFG_MAKE_BUSNUM(RP1_PRIM_BUS, RP1_SEC_BUS, RP1_SUB_BUS)

/*
 * Device IDs / BaseAddrs
 * These macros must exist in your BSP if you actually have two instances.
 * If your design uses different names, update these.
 */
#ifndef SDT
#ifndef XPAR_XDMAPCIE_0_DEVICE_ID
#error "XPAR_XDMAPCIE_0_DEVICE_ID not found. Check your xparameters.h / Vivado design."
#endif
#ifndef XPAR_XDMAPCIE_1_DEVICE_ID
#error "XPAR_XDMAPCIE_1_DEVICE_ID not found. Your BSP may not have a second XDMA PCIe instance."
#endif
#define XDMAPCIE0_DEVICE_ID   XPAR_XDMAPCIE_0_DEVICE_ID
#define XDMAPCIE1_DEVICE_ID   XPAR_XDMAPCIE_1_DEVICE_ID
#else
#ifndef XPAR_XXDMAPCIE_0_BASEADDR
#error "XPAR_XXDMAPCIE_0_BASEADDR not found. Check your xparameters.h / SDT design."
#endif
#ifndef XPAR_XXDMAPCIE_1_BASEADDR
#error "XPAR_XXDMAPCIE_1_BASEADDR not found. Your BSP may not have a second XDMA PCIe instance."
#endif
#define XDMAPCIE0_BASEADDR    XPAR_XXDMAPCIE_0_BASEADDR
#define XDMAPCIE1_BASEADDR    XPAR_XXDMAPCIE_1_BASEADDR
#endif

/**************************** Type Definitions ******************************/
/************************** Function Prototypes *****************************/

#ifndef SDT
static int PcieInitRootComplex(XDmaPcie *XdmaPciePtr, u16 DeviceId, u32 BusNumValue);
#else
static int PcieInitRootComplex(XDmaPcie *XdmaPciePtr, UINTPTR BaseAddress, u32 BusNumValue);
#endif

static void PrintRequesterId(XDmaPcie *XdmaPciePtr, const char *Tag);

/************************** Variable Definitions ****************************/

/* Allocate PCIe Root Complex IP Instances */
static XDmaPcie XdmaPcieInstance0;
static XDmaPcie XdmaPcieInstance1;

#if defined(QDMA_PCIE_BRIDGE)
extern XDmaPcie_Config XQdmaPcie_ConfigTable[];
#endif

/****************************************************************************/
/**
* Entry point
*****************************************************************************/
int main(void)
{
    int Status;

    xil_printf("\r\n=== XDMA/QDMA PCIe TWO-ROOT-PORT ENUMERATION EXAMPLE ===\r\n");

#ifndef SDT
    xil_printf("\r\n[RP0] Initializing Root Port 0...\r\n");
    Status = PcieInitRootComplex(&XdmaPcieInstance0, XDMAPCIE0_DEVICE_ID, RP0_BUSNUM_VALUE);
    if (Status != XST_SUCCESS) {
        xil_printf("[RP0] Init failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("[RP0] Enumerating fabric behind Root Port 0...\r\n");
    XDmaPcie_EnumerateFabric(&XdmaPcieInstance0);

    xil_printf("\r\n[RP1] Initializing Root Port 1...\r\n");
    Status = PcieInitRootComplex(&XdmaPcieInstance1, XDMAPCIE1_DEVICE_ID, RP1_BUSNUM_VALUE);
    if (Status != XST_SUCCESS) {
        xil_printf("[RP1] Init failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("[RP1] Enumerating fabric behind Root Port 1...\r\n");
    XDmaPcie_EnumerateFabric(&XdmaPcieInstance1);
#else
    xil_printf("\r\n[RP0] Initializing Root Port 0...\r\n");
    Status = PcieInitRootComplex(&XdmaPcieInstance0, XDMAPCIE0_BASEADDR, RP0_BUSNUM_VALUE);
    if (Status != XST_SUCCESS) {
        xil_printf("[RP0] Init failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("[RP0] Enumerating fabric behind Root Port 0...\r\n");
    XDmaPcie_EnumerateFabric(&XdmaPcieInstance0);

    xil_printf("\r\n[RP1] Initializing Root Port 1...\r\n");
    Status = PcieInitRootComplex(&XdmaPcieInstance1, XDMAPCIE1_BASEADDR, RP1_BUSNUM_VALUE);
    if (Status != XST_SUCCESS) {
        xil_printf("[RP1] Init failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("[RP1] Enumerating fabric behind Root Port 1...\r\n");
    XDmaPcie_EnumerateFabric(&XdmaPcieInstance1);
#endif

    xil_printf("\r\n=== DONE: Successfully ran TWO-ROOT-PORT enumeration example ===\r\n");
    return XST_SUCCESS;
}

/****************************************************************************/
/**
* Initialize one XDMA/QDMA PCIe IP configured as Root Complex.
*
* This is basically your original PcieInitRootComplex(), with one key change:
* we pass in the Primary/Secondary/Subordinate bus numbering value so each
* Root Port uses a different downstream bus range.
******************************************************************************/
#ifndef SDT
static int PcieInitRootComplex(XDmaPcie *XdmaPciePtr, u16 DeviceId, u32 BusNumValue)
#else
static int PcieInitRootComplex(XDmaPcie *XdmaPciePtr, UINTPTR BaseAddress, u32 BusNumValue)
#endif
{
    int Status;
    u32 HeaderData;
    u32 InterruptMask;
    int Retries;

    XDmaPcie_Config *ConfigPtr;

#ifndef SDT
    ConfigPtr = XDmaPcie_LookupConfig(DeviceId);
#else
    ConfigPtr = XDmaPcie_LookupConfig(BaseAddress);
#endif

#if defined(QDMA_PCIE_BRIDGE)
    // Use fixed config from qdma_config.c instead of autogenerated xdmapcie_g.c
    // NOTE: This assumes DeviceId indexing matches your config table.
#ifndef SDT
    ConfigPtr = &XQdmaPcie_ConfigTable[DeviceId];
#endif
#endif

    if (ConfigPtr == NULL) {
        xil_printf("ERROR: XDmaPcie_LookupConfig returned NULL\r\n");
        return XST_FAILURE;
    }

    Status = XDmaPcie_CfgInitialize(XdmaPciePtr, ConfigPtr, ConfigPtr->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Failed to initialize PCIe IP instance\r\n");
        return XST_FAILURE;
    }

    if (!XdmaPciePtr->Config.IncludeRootComplex) {
        xil_printf("ERROR: This PCIe IP is configured as ENDPOINT, not Root Complex\r\n");
        return XST_FAILURE;
    }

    /* Interrupt hygiene */
    XDmaPcie_GetEnabledInterrupts(XdmaPciePtr, &InterruptMask);
    xil_printf("Interrupts currently enabled: 0x%08X\r\n", InterruptMask);

    XDmaPcie_DisableInterrupts(XdmaPciePtr, XDMAPCIE_IM_ENABLE_ALL_MASK);

    XDmaPcie_GetPendingInterrupts(XdmaPciePtr, &InterruptMask);
    xil_printf("Interrupts pending:          0x%08X\r\n", InterruptMask);

    XDmaPcie_ClearPendingInterrupts(XdmaPciePtr, XDMAPCIE_ID_CLEAR_ALL_MASK);

    XDmaPcie_GetEnabledInterrupts(XdmaPciePtr, &InterruptMask);
    XDmaPcie_GetPendingInterrupts(XdmaPciePtr, &InterruptMask);

    /* Wait for link up */
    Status = FALSE;
    for (Retries = 0; Retries < XDMAPCIE_LINK_WAIT_MAX_RETRIES; Retries++) {
        if (XDmaPcie_IsLinkUp(XdmaPciePtr)) {
            Status = TRUE;
            break;
        }
        usleep(XDMAPCIE_LINK_WAIT_USLEEP_MIN);
    }

    if (Status != TRUE) {
        xil_printf("ERROR: Link is not up\r\n");
        return XST_FAILURE;
    }

    xil_printf("Link is up\r\n");
    PrintRequesterId(XdmaPciePtr, "Requester ID");

    /* Enable Root Port command bits: IO/MEM/BUS MASTER + error enables */
    XDmaPcie_ReadLocalConfigSpace(XdmaPciePtr, PCIE_CFG_CMD_STATUS_REG, &HeaderData);

    HeaderData |= (PCIE_CFG_CMD_BUSM_EN |
                   PCIE_CFG_CMD_MEM_EN  |
                   PCIE_CFG_CMD_IO_EN   |
                   PCIE_CFG_CMD_PARITY  |
                   PCIE_CFG_CMD_SERR_EN);

    XDmaPcie_WriteLocalConfigSpace(XdmaPciePtr, PCIE_CFG_CMD_STATUS_REG, HeaderData);

    XDmaPcie_ReadLocalConfigSpace(XdmaPciePtr, PCIE_CFG_CMD_STATUS_REG, &HeaderData);
    xil_printf("Local Config CommandStatus: 0x%08X\r\n", HeaderData);

    /* Program bus numbers (Primary/Secondary/Subordinate) for this Root Port */
    XDmaPcie_WriteLocalConfigSpace(XdmaPciePtr, PCIE_CFG_PRI_SEC_BUS_REG, BusNumValue);

    XDmaPcie_ReadLocalConfigSpace(XdmaPciePtr, PCIE_CFG_PRI_SEC_BUS_REG, &HeaderData);
    xil_printf("Local Config Prim/Sec/Sub:  0x%08X\r\n", HeaderData);
    xil_printf("Root Port initialized.\r\n");

    return XST_SUCCESS;
}

/****************************************************************************/
/**
* Print Requester ID (Bus/Device/Function/Port) for debugging.
*****************************************************************************/
static void PrintRequesterId(XDmaPcie *XdmaPciePtr, const char *Tag)
{
    u8 BusNumber = 0;
    u8 DeviceNumber = 0;
    u8 FunNumber = 0;
    u8 PortNumber = 0;

    XDmaPcie_GetRequesterId(XdmaPciePtr, &BusNumber, &DeviceNumber, &FunNumber, &PortNumber);

    xil_printf("%s:\r\n", Tag);
    xil_printf("  Bus      = 0x%02X\r\n", BusNumber);
    xil_printf("  Device   = 0x%02X\r\n", DeviceNumber);
    xil_printf("  Function = 0x%02X\r\n", FunNumber);
    xil_printf("  Port     = 0x%02X\r\n", PortNumber);
}
