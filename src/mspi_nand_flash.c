/*
 * mspi_nand_flash.h
 * Driver for QuadSPI NAND Flash using CE0
 * Chris Hemingway, 2019
 */

//*****************************************************************************
//
// Functions mspi_nand_flash_init, am_device_command_write, am_device_command_read 
// Copyright (c) 2019, Ambiq Micro
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
// 
// Third party software included in this distribution is subject to the
// additional license terms as defined in the /docs/licenses directory.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//*****************************************************************************

#include "mspi_nand_flash.h"
#include "am_util_stdio.h"
#include "am_util_delay.h"
#include "am_bsp.h"


#define AM_DEVICES_MSPI_FLASH_TIMEOUT             1000000

#if defined(MICRON_MT29F8G01AD)

#define CMD_READ_CACHE_SINGLE 0x03
#define CMD_PROGRAM_RANDOM_SINGLE 0x84
#define CMD_RESET 0xFF
#define CMD_READ_ID 0x9f

#define CMD_READ_CACHE_QUADIO 0xEB
#define CMD_PROGRAM_RANDOM_QUAD 0x34

#define NAND_FLASH_ID       0x2c46
#define NAND_FLASH_ID_MASK  0xfffe  // Allow both 0x2c46 (3.3V) and 0x2c47 (1.8V)

#define RESET_TIME_MS 1 // Takes 565uS to reset, round up to 1ms

// NAND Flash device configuration structure
am_hal_mspi_dev_config_t  g_psMSPISettings =
{
    .eSpiMode             = AM_HAL_MSPI_SPI_MODE_0, // See micron datasheet
    .eClockFreq           = AM_HAL_MSPI_CLK_1P5MHZ,

    .ui8TurnAround        = 3,
    .eAddrCfg             = AM_HAL_MSPI_ADDR_2_BYTE, // TODO: How to map column address?

    .eInstrCfg            = AM_HAL_MSPI_INSTR_1_BYTE,       // One byte SPI
    .eDeviceConfig        = AM_HAL_MSPI_FLASH_SERIAL_CE0,   // Single SPI
    .bSeparateIO          = true,                           // Seperate MOSI/MISO pins
    .bSendInstr           = true,                           // Send instruction
    .bSendAddr            = true,                           // Enable sending address
    .bTurnaround          = true,                           // Enable turnaround
    // TODO: These are for use with DMA, check if OK
    .ui8ReadInstr         = CMD_READ_CACHE_SINGLE,      
    .ui8WriteInstr        = CMD_PROGRAM_RANDOM_SINGLE,
    .ui32TCBSize          = 0,                              // No DMA Transfer Control Buffer
    .pTCB                 = NULL,
    .scramblingStartAddr  = 0,                              // No data scrambling
    .scramblingEndAddr    = 0,
};
#else
#error "No Flash defined!"
#endif

// Pointer to MSPI peripheral
void                            *g_pMSPIHandle;   


// Transaction state and buffer
am_hal_mspi_pio_transfer_t      g_PIOTransaction;
uint32_t                        g_PIOBuffer[32];

const uint32_t ui32Module = 0;

//*****************************************************************************
//
// Generic Command Write function.
//
//*****************************************************************************
uint32_t am_device_command_write(uint32_t ui32Module, uint8_t ui8Instr, bool bSendAddr,
                                 uint32_t ui32Addr, uint32_t *pData,
                                 uint32_t ui32NumBytes)
{
    uint32_t ui32Status;

    // Create the individual write transaction.
    g_PIOTransaction.ui32NumBytes       = ui32NumBytes;
    g_PIOTransaction.eDirection         = AM_HAL_MSPI_TX;
    g_PIOTransaction.bSendAddr          = bSendAddr;
    g_PIOTransaction.ui32DeviceAddr     = ui32Addr;
    g_PIOTransaction.bSendInstr         = true;
    g_PIOTransaction.ui16DeviceInstr    = ui8Instr;
    g_PIOTransaction.bTurnaround        = false;
#if 0 // A3DS-25 Deprecate MSPI CONT
    g_PIOTransaction.bContinue          = false;
#endif // A3DS-25

    if (AM_HAL_MSPI_FLASH_QUADPAIRED == g_psMSPISettings.eDeviceConfig)
    {
        g_PIOTransaction.bQuadCmd         = true;
    }
    else
    {
        g_PIOTransaction.bQuadCmd         = false;
    }

    g_PIOTransaction.pui32Buffer        = pData;

#if defined (MSPI_XIPMIXED)
    am_hal_mspi_dev_config_t mode = am_devices_mspi_flash_mode_switch(ui32Module, &SerialCE0MSPIConfig);
#endif

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);
#if defined (MSPI_XIPMIXED)
    am_devices_mspi_flash_mode_switch(ui32Module, &mode);
#endif

    return ui32Status;
}

//*****************************************************************************
//
// Generic Command Read function.
//
//*****************************************************************************
uint32_t am_device_command_read(uint32_t ui32Module, uint8_t ui8Instr, bool bSendAddr,
                                uint32_t ui32Addr, uint32_t *pData,
                                uint32_t ui32NumBytes)
{
    uint32_t ui32Status;

    // Create the individual write transaction.
    g_PIOTransaction.eDirection         = AM_HAL_MSPI_RX;
    g_PIOTransaction.bSendAddr          = bSendAddr;
    g_PIOTransaction.ui32DeviceAddr     = ui32Addr;
    g_PIOTransaction.bSendInstr         = true;
    g_PIOTransaction.ui16DeviceInstr    = ui8Instr;
    g_PIOTransaction.bTurnaround        = false;
#if 0 // A3DS-25 Deprecate MSPI CONT
    g_PIOTransaction.bContinue          = false;
#endif // A3DS-25

    if (AM_HAL_MSPI_FLASH_QUADPAIRED == g_psMSPISettings.eDeviceConfig)
    {
        g_PIOTransaction.ui32NumBytes     = ui32NumBytes * 2;
        g_PIOTransaction.bQuadCmd      = true;
    }
    else
    {
        g_PIOTransaction.ui32NumBytes     = ui32NumBytes;
        g_PIOTransaction.bQuadCmd      = false;
    }

    g_PIOTransaction.pui32Buffer        = pData;

#if defined (MSPI_XIPMIXED)
    am_hal_mspi_dev_config_t mode = am_devices_mspi_flash_mode_switch(AM_DEVICES_MSPI_FLASH_MSPI_INSTANCE, &SerialCE0MSPIConfig);
#endif

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);
#if defined (MSPI_XIPMIXED)
    am_devices_mspi_flash_mode_switch(AM_DEVICES_MSPI_FLASH_MSPI_INSTANCE, &mode);
#endif

    return ui32Status;
}



uint32_t mspi_nand_flash_reset(uint32_t ui32Module)
{

  if (AM_HAL_STATUS_SUCCESS != am_device_command_write(ui32Module, CMD_RESET, false, 0, g_PIOBuffer, 0))
  {
    return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
  }
  am_util_delay_ms(RESET_TIME_MS);

  return AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS;
}



uint32_t mspi_nand_flash_init(void **pHandle)
{
    uint32_t      ui32Status;

    //
    // Enable fault detection.
    //
#if AM_APOLLO3_MCUCTRL
    am_hal_mcuctrl_control(AM_HAL_MCUCTRL_CONTROL_FAULT_CAPTURE_ENABLE, 0);
#else // AM_APOLLO3_MCUCTRL
    am_hal_mcuctrl_fault_capture_enable();
#endif // AM_APOLLO3_MCUCTRL

    //
    // Configure the MSPI, Quad SPI
    //
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_initialize(ui32Module, &g_pMSPIHandle))
    {
        am_util_stdio_printf("Error - Failed to initialize MSPI.\n");
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }

    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_power_control(g_pMSPIHandle, AM_HAL_SYSCTRL_WAKE, false))
    {
        am_util_stdio_printf("Error - Failed to power on MSPI.\n");
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }

    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_device_configure(g_pMSPIHandle, &g_psMSPISettings))
    {
        am_util_stdio_printf("Error - Failed to configure MSPI.\n");
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_enable(g_pMSPIHandle))
    {
        am_util_stdio_printf("Error - Failed to enable MSPI.\n");
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
    am_bsp_mspi_pins_enable(ui32Module, g_psMSPISettings.eDeviceConfig);
        


    if (AM_HAL_STATUS_SUCCESS != mspi_nand_flash_reset(ui32Module))
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }

    //
    // TODO: Device specific MSPI Flash initialization.
    //
    #if 0
    ui32Status = am_device_init_flash(ui32Module, g_psMSPISettings);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
    #endif

    //
    // Initialize the MSPI settings for the MSPI_FLASH.
    //

    // Disable MSPI defore re-configuring it
    ui32Status = am_hal_mspi_disable(g_pMSPIHandle);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
    //
    // Re-Configure the MSPI for the requested operation mode.
    //
    ui32Status = am_hal_mspi_device_configure(g_pMSPIHandle, &g_psMSPISettings);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
    // Re-Enable MSPI
    ui32Status = am_hal_mspi_enable(g_pMSPIHandle);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }

    //
    // Configure the MSPI pins.
    //
    am_bsp_mspi_pins_enable(ui32Module, g_psMSPISettings.eDeviceConfig);

    //
    // Enable MSPI interrupts.
    //

    ui32Status = am_hal_mspi_interrupt_clear(g_pMSPIHandle, AM_HAL_MSPI_INT_CQUPD | AM_HAL_MSPI_INT_ERR );
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }

    ui32Status = am_hal_mspi_interrupt_enable(g_pMSPIHandle, AM_HAL_MSPI_INT_CQUPD | AM_HAL_MSPI_INT_ERR );
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }


    NVIC_EnableIRQ(MSPI0_IRQn);

    am_hal_interrupt_master_enable();

    //
    // Return the handle.
    //
    *pHandle = g_pMSPIHandle;

    //
    // Return the status.
    //
    return AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS;
}

uint32_t mspi_nand_flash_id(void)
{
    uint32_t      ui32Status;
    uint32_t      ui32DeviceID;

    // Send device ID command, no address, and read 3 bytes (one dummy)
    ui32Status = am_device_command_read(ui32Module, CMD_READ_ID, false, 0, &ui32DeviceID, 3);
    am_util_stdio_printf("Flash ID is %8.8X\n", ui32DeviceID);
    
    if ( ((ui32DeviceID & NAND_FLASH_ID_MASK) == NAND_FLASH_ID) &&
       (AM_HAL_STATUS_SUCCESS == ui32Status) )
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS;
    }
    else
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
}