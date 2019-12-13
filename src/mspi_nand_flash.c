/*
 * mspi_nand.h
 * Driver for QuadSPI NAND Flash using CE0
 * Chris Hemingway, 2019
 */

//*****************************************************************************
//
// Functions mspi_nand_init, am_device_command_write, am_device_command_read 
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

#include <string.h> // memcmp

#include "mspi_nand_flash.h"
#include "onfi_print.h"

#include "am_util_stdio.h"
#include "am_util_delay.h"
#include "am_bsp.h"

// Disable warning for unused functions in this file
#pragma GCC diagnostic ignored "-Wunused-function"

#define AM_DEVICES_MSPI_FLASH_TIMEOUT             1000000

#if defined(MICRON_MT29F8G01AD)

#define CMD_RESET 0xFF

#define CMD_WRITE_ENABLE        0x06
#define CMD_WRITE_DISABLE       0x04

#define CMD_PAGE_READ           0x13
#define CMD_READ_CACHE_SINGLE   0x03

#define CMD_READ_ID             0x9f
#define CMD_READ_CACHE_X4       0x6B
#define CMD_READ_CACHE_QUADIO   0xEB

#define CMD_PROGRAM_LOAD        0x02
#define CMD_PROGRAM_EXECUTE     0x10
#define CMD_PROGRAM_LOAD_RANDOM 0x84
#define CMD_PROGRAM_LOAD_RANDOM_QUAD 0x34

#define CMD_GET_FEATURES        0x0F
#define CMD_SET_FEATURES        0x1F

#define FEATURE_REG_BLOCK_LOCK 0xA0    // Block lock feature register

#define FEATURE_REG_CONFIG     0xB0                      // Config feature register
#define FEATURE_REG_CONFIG_CFG_MASK                 0xC1 // Config register CFG bits mask
#define FEATURE_REG_CONFIG_CFG_VALUE_READ_PARAMS    0x40 // Config:CFG value for read parameter page


#define FEATURE_REG_STATUS     0xC0         // Status register
#define FEATURE_REG_STATUSCACHE_READ_BUSY     0x07    // Mask for Cache Read Busy (CRBSY)
// TODO: ECC Status bits 0:2
#define FEATURE_REG_STATUS_P_FAIL_MASK  0x04    // Mask for program failure
#define FEATURE_REG_STATUS_E_FAIL_MASK  0x04    // Mask for erase failure
#define FEATURE_REG_STATUS_WEL_MASK     0x02    // Mask for Write Enable Latch (WEL) bit. 1=Writable
#define FEATURE_REG_STATUS_OIP_MASK     0x01    // Mask for Operation In Progress (OIP) bit. 1=Busy

#define FEATURE_REG_DIE_SELECT 0xD0    // Die select register

// #define nand_ID       0x462c
#define nand_ID       0x252c  // Byte reversed, LSB is first byte
#define nand_ID_MASK  0xfeff  // Allow both 0x2c46 (3.3V) and 0x2c47 (1.8V)

#define RESET_TIME_MS 1 // Takes 565uS to reset, round up to 1ms

#define PAGE_SIZE   (128+(2*1024))  // Page is 128 Metadata/ECC + 2K Data

#define PAGES_PER_BLOCK 64
#define LOG2_PPB        6
#define NUM_BLOCKS      2048

#define PARAMETER_PAGE_SIZE 256        // Parameter page is 255 bytes, repeats up to PAGE_SIZE
#define PARAMETER_PAGE_PAGE_ADDR 0x01  // Parameter table is page 0x01 (only in OTP/Param access mode)
#define PARAMETER_PAGE_COLUMN_ADDR 0x00 // Parameter table starts at column 0x00

#define BAD_BLOCK_PAGE_OFFSET 0x00      // Page within block that bad block marking is in
#define BAD_BLOCK_BYTE_OFFSET 2048      // Offset of bad block within page (column address)
#define BAD_BLOCK_MARKER_VALUE  0x00    // 0x00 in byte 2048 (1st spare) = bad block     

// NAND Flash device configuration structure
am_hal_mspi_dev_config_t  g_psMSPISettings =
{
    .eSpiMode             = AM_HAL_MSPI_SPI_MODE_0, // See micron datasheet
    .eClockFreq           = AM_HAL_MSPI_CLK_6MHZ,

    .ui8TurnAround        = 8,                       // For READ FROM CACHEx1 , 1 dummy byte = 8 bits
    .eAddrCfg             = AM_HAL_MSPI_ADDR_2_BYTE, // Read address is 13 bit (12 addr + pane) and 3 dummy bits = 16

    .eInstrCfg            = AM_HAL_MSPI_INSTR_1_BYTE,       // One byte SPI
    .eDeviceConfig        = AM_HAL_MSPI_FLASH_SERIAL_CE0,   // Single SPI, CS0
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

#else // Not defined flash ID
#error "No Flash defined!"
#endif

// Pointer to MSPI peripheral
void                            *g_pMSPIHandle;   

// Transaction state
am_hal_mspi_pio_transfer_t      g_PIOTransaction;

// Buffer for entire page read/write test
// Align for DMA to word size, TODO: Is this needed?
uint8_t                         page_buffer[PAGE_SIZE] __attribute__((aligned(4)));


const uint32_t ui32Module = 0; // Index of MSPI module. Apollo3 only has MSPI0


// Convert a block number into a block/page address
static inline uint32_t block_to_page_addr(uint32_t block_addr) {
    return block_addr << LOG2_PPB;
}


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



uint32_t mspi_nand_reset(void)
{

  if (AM_HAL_STATUS_SUCCESS != am_device_command_write(ui32Module, CMD_RESET, false, 0, NULL, 0))
  {
    return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
  }
  am_util_delay_ms(RESET_TIME_MS);

  return AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS;
}



uint32_t mspi_nand_init(void **pHandle)
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
        


    if (AM_HAL_STATUS_SUCCESS != mspi_nand_reset())
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
    // Here we change to QuadSPI mode, so the pins are enabled for this
    //
    am_hal_mspi_device_e pins_device_config = g_psMSPISettings.eDeviceConfig;
    pins_device_config = AM_HAL_MSPI_FLASH_QUAD_CE0; //TODO: Make chip select configurable
    am_bsp_mspi_pins_enable(ui32Module, pins_device_config);

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


/*
 * Private function to change device to/from SPI to/from QuadSPI
 */
static uint32_t mspi_set_use_quadspi(bool use_quadspi) {
    uint32_t ui32Status;

    // Disable MSPI defore re-configuring it
    ui32Status = am_hal_mspi_disable(g_pMSPIHandle);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
    //
    // Re-Configure the MSPI for the requested operation mode.
    //
    if (use_quadspi) {
        g_psMSPISettings.eDeviceConfig = AM_HAL_MSPI_FLASH_QUAD_CE0;
        g_psMSPISettings.eXipMixedMode = AM_HAL_MSPI_XIPMIXED_D4; // 1:1:4 for instruction, address, timing
    } else {
        g_psMSPISettings.eDeviceConfig = AM_HAL_MSPI_FLASH_SERIAL_CE0;
        g_psMSPISettings.eXipMixedMode = AM_HAL_MSPI_XIPMIXED_NORMAL; // 1:1:4 for instruction, address, timing
    }
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

    return AM_HAL_STATUS_SUCCESS;
}


uint32_t mspi_nand_id(void)
{
    uint32_t      ui32Status;
    uint32_t      ui32DeviceID;

    // Send device ID command, no address, and read 3 bytes (one dummy)
    ui32Status = am_device_command_read(ui32Module, CMD_READ_ID, false, 0, &ui32DeviceID, 3);
    // Shift out dummy byte
    ui32DeviceID >>= 8;
    
    // Check byte is valid
    if ( ((ui32DeviceID & nand_ID_MASK) == (nand_ID & nand_ID_MASK)) &&
       (AM_HAL_STATUS_SUCCESS == ui32Status) )
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS;
    }
    else
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }
}

/* 
 * Execute the GET_FEATURES command given a register address addr, to get the byte data
 * Not part of public API, as higher level functions (e.g. get status) should be used
 */
static uint32_t mspi_nand_cmd_get_features(uint8_t addr, uint8_t *data) {

    uint32_t ui32Status;
    uint32_t returned_data = 0;

    // The GET_FEATURES command uses 1 byte addresses, unlike all others 
    // HACK: Change the address size directly, rather than through HAL, as 
    // using HAL requires us to reconfigure entire peripheral.
    uint32_t mspi_cfg_old_asize = MSPI->CFG_b.ASIZE;
    MSPI->CFG_b.ASIZE = 0x00;   // Address is 1 byte

    // Create the individual write transaction.
    g_PIOTransaction.eDirection         = AM_HAL_MSPI_RX;
    g_PIOTransaction.bSendAddr          = true;    // Send address
    g_PIOTransaction.ui32DeviceAddr     = addr;
    g_PIOTransaction.bSendInstr         = true;     // Send instruction, 1 byte
    g_PIOTransaction.ui16DeviceInstr    = CMD_GET_FEATURES;
    g_PIOTransaction.bTurnaround        = false;
    g_PIOTransaction.ui32NumBytes       = 1;        // 1 byte read
    g_PIOTransaction.bQuadCmd           = false;    // SPI only, no quad or octal
    g_PIOTransaction.pui32Buffer        = &returned_data;    // Read into buffer given

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);

    // Reset instruction size to original config
    MSPI->CFG_b.ASIZE = mspi_cfg_old_asize;

    // Convert recieved from uint32_t to uint8_t
    *data = (uint8_t)(returned_data & 0xff);

    return ui32Status;
}


/* 
 * Execute the SET_FEATURES command given a register address addr, to get the byte data
 * Not part of public API, as higher level functions (e.g. get status) should be used
 */
static uint32_t mspi_nand_cmd_set_features(uint8_t addr, uint8_t data) {

    uint32_t ui32Status;
    uint32_t data32 = data;

    // The GET_FEATURES command uses 1 byte addresses, unlike all others 
    // HACK: Change the address size directly, rather than through HAL, as 
    // using HAL requires us to reconfigure entire peripheral.
    uint32_t mspi_cfg_old_asize = MSPI->CFG_b.ASIZE;
    MSPI->CFG_b.ASIZE = 0x00;   // Address is 1 byte

    // Create the individual write transaction.
    g_PIOTransaction.eDirection         = AM_HAL_MSPI_TX;
    g_PIOTransaction.bSendAddr          = true;    // Send address
    g_PIOTransaction.ui32DeviceAddr     = addr;
    g_PIOTransaction.bSendInstr         = true;     // Send instruction, 1 byte
    g_PIOTransaction.ui16DeviceInstr    = CMD_SET_FEATURES;
    g_PIOTransaction.bTurnaround        = false;
    g_PIOTransaction.ui32NumBytes       = 1;        // 1 byte write
    g_PIOTransaction.bQuadCmd           = false;    // SPI only, no quad or octal
    g_PIOTransaction.pui32Buffer        = &data32;    // Write 32 bit data

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);

    // Reset instruction size to original config
    MSPI->CFG_b.ASIZE = mspi_cfg_old_asize;

    return ui32Status;
}


uint32_t mspi_nand_write_enable(void) {
    uint32_t      ui32Status;

    // Send write_enable command, no address, no data
    ui32Status = am_device_command_write(ui32Module, CMD_WRITE_ENABLE, false, 0, NULL, 0);
    return ui32Status;
}


uint32_t mspi_nand_write_disable(void) {
    uint32_t      ui32Status;

    // Send write_enable command, no address, no data
    ui32Status = am_device_command_write(ui32Module, CMD_WRITE_DISABLE, false, 0, NULL, 0);
    return ui32Status;
}


uint32_t mspi_nand_get_writable(bool *writable) {
    uint32_t    ui32Status;
    uint8_t    status_reg;

    // Get the status register
    ui32Status = mspi_nand_cmd_get_features(FEATURE_REG_STATUS, &status_reg);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) { // Exit early on error
        return ui32Status;
    }

    if (status_reg & FEATURE_REG_STATUS_WEL_MASK) {
        *writable = true;
    } else {
        *writable = false;
    }

    return ui32Status;
}


uint32_t mspi_nand_get_busy(bool *busy) {
    uint32_t    ui32Status;
    uint8_t    status_reg;

    // Get the status register
    ui32Status = mspi_nand_cmd_get_features(FEATURE_REG_STATUS, &status_reg);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) { // Exit early on error
        return ui32Status;
    }

    if (status_reg & FEATURE_REG_STATUS_OIP_MASK) {
        *busy = true;
    } else {
        *busy = false;
    }

    return ui32Status;
}


/* 
 * Execute PAGE_READ command to read a page into the cache given page address
 */
static uint32_t mspi_nand_cmd_page_read(uint32_t page_addr) {
    uint32_t ui32Status;

    // Change to 3 byte addresses 
    MSPI->CFG_b.ASIZE = 0x02;   // Address is 3 bytes
    ui32Status = am_device_command_write(ui32Module, CMD_PAGE_READ, true, page_addr, NULL, 0);
    return ui32Status;
}


/* 
 * Execute READ FROM CACHE x1 to read single page into Cache
 */
static uint32_t mspi_nand_cmd_read_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
    uint32_t ui32Status;
    uint32_t addr_plus_dummy = 0;

    // Hack: To make the MSPI peripheral ignore the dummy byte, we use 3 byte addresses 
    // and left shift the address by one byte
    MSPI->CFG_b.ASIZE = 0x02;   // Address is 3 bytes (but we keep byte 0 empty as dummy)

    addr_plus_dummy = (uint32_t)column_addr << 8; // Move along, leave blank space

    ui32Status = am_device_command_read(ui32Module, CMD_READ_CACHE_SINGLE, true, addr_plus_dummy, data , data_len);

    return ui32Status;
}

/* 
 * Execute READ FROM CACHE x4 to read single page into Cache with x1 instruction + address, x4 data
 * FIXME: Not currently working! Instr is sent as Quad, not SPI as it should be
 */
static uint32_t mspi_nand_cmd_read_x4(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
    uint32_t ui32Status;

    // Change to 2 byte addresses
    MSPI->CFG_b.ASIZE = 0x01;   // Address is 2 bytes

    // 4 Turnaround cycles
    MSPI->CFG_b.TURNAROUND = 4;

    // Change to Quad mode
    ui32Status = mspi_set_use_quadspi(true);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Create the individual write transaction.
    g_PIOTransaction.eDirection         = AM_HAL_MSPI_RX;
    g_PIOTransaction.bSendAddr          = true;
    g_PIOTransaction.ui32DeviceAddr     = column_addr;
    g_PIOTransaction.bSendInstr         = true;
    g_PIOTransaction.ui16DeviceInstr    = CMD_READ_CACHE_X4;
    g_PIOTransaction.bTurnaround        = true;
    g_PIOTransaction.ui32NumBytes       = data_len;
    g_PIOTransaction.bQuadCmd           = true;    // Command is _not_ quad, only address + data

    g_PIOTransaction.pui32Buffer        = data;

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);

    // Change back to SPI mode
    ui32Status = mspi_set_use_quadspi(false);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    return ui32Status;
}


/* 
 * Execute READ FROM CACHE Quad I/O to read single page into Cache
 * FIXME: Not currently working! Instr is sent as Quad, not SPI as it should be
 */
static uint32_t mspi_nand_cmd_read_quadio(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
    uint32_t ui32Status;

    // Change to 2 byte addresses
    MSPI->CFG_b.ASIZE = 0x01;   // Address is 2 bytes

    // 4 Turnaround cycles
    MSPI->CFG_b.TURNAROUND = 4;

    // Change to Quad mode
    ui32Status = mspi_set_use_quadspi(true);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Create the individual write transaction.
    g_PIOTransaction.eDirection         = AM_HAL_MSPI_RX;
    g_PIOTransaction.bSendAddr          = true;
    g_PIOTransaction.ui32DeviceAddr     = column_addr;
    g_PIOTransaction.bSendInstr         = true;
    g_PIOTransaction.ui16DeviceInstr    = CMD_READ_CACHE_QUADIO;
    g_PIOTransaction.bTurnaround        = true;
    g_PIOTransaction.ui32NumBytes       = data_len;
    g_PIOTransaction.bQuadCmd           = false;    // Command is _not_ quad, only address + data

    g_PIOTransaction.pui32Buffer        = data;

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);

    // Change back to SPI mode
    ui32Status = mspi_set_use_quadspi(false);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    return ui32Status;
}

/* 
 * Execute Program Load x1 to write single page into cache
 */
static uint32_t mspi_nand_cmd_program_load_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
    uint32_t ui32Status;

    // Change to 2 byte addresses
    MSPI->CFG_b.ASIZE = 0x01;   // Address is 2 bytes

    // No turnaround, immediately send data
    MSPI->CFG_b.TURNAROUND = 0;

    ui32Status = am_device_command_write(ui32Module, CMD_PROGRAM_LOAD, true, column_addr, data , data_len);

    return ui32Status;
}


/* 
 * Execute Random Data Program x1, writes to cache _without_ clearing it
 */
static uint32_t mspi_nand_cmd_program_load_random_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
    uint32_t ui32Status;

    // Change to 2 byte addresses
    MSPI->CFG_b.ASIZE = 0x01;   // Address is 2 bytes

    // No turnaround, immediately send data
    MSPI->CFG_b.TURNAROUND = 0;

    ui32Status = am_device_command_write(ui32Module, CMD_PROGRAM_LOAD_RANDOM, true, column_addr, data , data_len);

    return ui32Status;
}


/* 
 * Execute PROGRAM_EXECUTE to write a page from the cache given page address
 */
static uint32_t mspi_nand_cmd_program_execute(uint32_t page_addr) {
    uint32_t ui32Status;

    // Change to 3 byte addresses 
    MSPI->CFG_b.ASIZE = 0x02;   // Address is 3 bytes
    ui32Status = am_device_command_write(ui32Module, CMD_PROGRAM_EXECUTE, true, page_addr, NULL, 0);
    return ui32Status;
}


/*
 * Read the parameter page (blocking) from the flash into *params_page of size len
 * Len must be > PARAMETER_PAGE_SIZE
 */
uint32_t mspi_nand_read_params_page(uint8_t *params_page, uint32_t len, bool use_quad) {
    uint32_t ui32Status;
    uint8_t  reg_config, old_reg_config;
    bool busy = true;

    // Check length parameter
    if (len < PARAMETER_PAGE_SIZE) {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    // Modify CFG bits of Feature register Configuration (0xB0) to get params page
    ui32Status = mspi_nand_cmd_get_features(FEATURE_REG_CONFIG, &old_reg_config);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    reg_config = old_reg_config & ~FEATURE_REG_CONFIG_CFG_MASK; // Clear CFG bits
    reg_config |= FEATURE_REG_CONFIG_CFG_VALUE_READ_PARAMS;     // Set CFG to VALUE_READ_PARAMS, 010

    mspi_nand_cmd_set_features(FEATURE_REG_CONFIG, reg_config);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Read params page into cache
    ui32Status = mspi_nand_cmd_page_read(PARAMETER_PAGE_PAGE_ADDR);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Spin until completed
    while(busy) {
        ui32Status = mspi_nand_get_busy(&busy);
        if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    }

    // Read out params page from cache
    if (use_quad) {
        ui32Status = mspi_nand_cmd_read_quadio(PARAMETER_PAGE_COLUMN_ADDR, (uint32_t *)params_page, len);
    } else {
        ui32Status = mspi_nand_cmd_read_x1(PARAMETER_PAGE_COLUMN_ADDR, (uint32_t *)params_page, len);
    }
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Write original value back to old_reg_config to exit parameter page reading mode
    mspi_nand_cmd_set_features(FEATURE_REG_CONFIG, old_reg_config);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    return AM_HAL_STATUS_SUCCESS; // Return success
}


uint32_t mspi_nand_check_bad_block(uint32_t block_addr, bool *is_bad) {
    uint32_t ui32Status, page_addr;
    uint8_t marker = ! BAD_BLOCK_MARKER_VALUE;
    bool busy = true;

    // Convert block addr into page addr (get first page)
    page_addr = block_to_page_addr(block_addr);

     // Read params page into cache
    ui32Status = mspi_nand_cmd_page_read(page_addr + BAD_BLOCK_PAGE_OFFSET);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Spin until completed
    // TODO: Timeout
    do {
        ui32Status = mspi_nand_get_busy(&busy);
        if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    } while (busy==true);

    // Read out single byte at marker address
    ui32Status = mspi_nand_cmd_read_x1(BAD_BLOCK_BYTE_OFFSET, (uint32_t *)&marker, 1);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Check for bad block marker
    *is_bad = (marker == BAD_BLOCK_MARKER_VALUE);
    // Success
    return ui32Status;
}


/*
 * Utility function to print the address of all bad blocks
 * Can take a few hundred ms or so
 * FIXME: Why are we not finding any bad blocks?
 */
uint32_t mspi_nand_print_bad_blocks(void) {
    bool is_bad;
    uint32_t ui32Status, total = 0;

    am_util_stdio_printf("Checking for Bad Blocks, addresses (decimal): \n");
    for (uint32_t i=0; i<NUM_BLOCKS; i++) {
        ui32Status = mspi_nand_check_bad_block(i, &is_bad);
        if (ui32Status != AM_HAL_STATUS_SUCCESS) {
            am_util_stdio_printf("Failed to read block %lu \n", i);
            return ui32Status;
        }
        if (is_bad) {
            am_util_stdio_printf("%lu \n", i);
            total++;
        }
    }
    am_util_stdio_printf("COMPLETE, found %lu \n", total);
    return ui32Status;
}


uint32_t mspi_nand_test(void) {
    bool writable = false, busy = false, is_bad = false;
    uint8_t params_page[PARAMETER_PAGE_SIZE];
    uint8_t quad_params_page[PARAMETER_PAGE_SIZE];

    // Quick test macros. TODO: Use a proper framework from someone else!
    #define STRINGIFY(x) #x
    #define TOSTRING(x) STRINGIFY(x)
    #define RET_CHECK(cmd) if(cmd != AM_HAL_STATUS_SUCCESS) {am_util_stdio_printf("Flash TEST: " STRINGIFY(cmd) " returned error \n"); return 1;}


    // Check for valid flash ID
    RET_CHECK(mspi_nand_id());

    // Enable write and check if writable status is correct
    RET_CHECK(mspi_nand_write_enable());
    RET_CHECK(mspi_nand_get_writable(&writable));
    if (writable == false) {
        am_util_stdio_printf("Flash TEST: Writable status was not enabled! \n");
        return 1;
    }
    // Enable write and check if writable status is correct
    RET_CHECK(mspi_nand_write_disable());
    RET_CHECK(mspi_nand_get_writable(&writable));
    if (writable == true) {
        am_util_stdio_printf("Flash TEST: Writable status was not disabled! \n");
        return 1;
    }
    

    // Test reading a page into cache
    // Should be immediately busy, then not busy after tRead
    // TODO: Check ECC?
    RET_CHECK(mspi_nand_cmd_page_read(0xa5)); // Read block a5 = 165, chosen for pattern
    RET_CHECK(mspi_nand_get_busy(&busy));
    if(busy == false) {
        am_util_stdio_printf("Flash TEST: Flash is not busy immediately after CMD_READ_PAGE! \n");
        return 1;
    }
    am_util_delay_us(80); // tRD is 80uS max with ECC enabled
    RET_CHECK(mspi_nand_get_busy(&busy));
    if(busy == true) {
        am_util_stdio_printf("Flash TEST: Flash is still busy 80uS after CMD_READ_PAGE! \n");
        return 1;
    }

    // Test writing a page into cache and reading it back. Do not actually program.
    for (int i=0;i<PAGE_SIZE;i++) {page_buffer[i] =i&0xff;} // Ascending bytes
    mspi_nand_cmd_program_load_x1(0, (uint32_t *)page_buffer, PAGE_SIZE);
    mspi_nand_cmd_read_x1(0, (uint32_t *)page_buffer, PAGE_SIZE);
    for (int i=0;i<PAGE_SIZE;i++) {
        if (page_buffer[i] != (i & 0xff)) {
            am_util_stdio_printf("Flash TEST: Read back wrong value from cache at %d, got %uud \n", 
                                 i, page_buffer[i]);
            break;
        }
    }

    // Test first block is not bad (should be good out of factory)
    RET_CHECK(mspi_nand_check_bad_block(0, &is_bad));
    if(is_bad) {
        am_util_stdio_printf("Flash TEST: Block 0 reads as bad, but should be good! \n");
        return AM_HAL_STATUS_FAIL;
    }

    // Read page from cache using x1 interface
    // RET_CHECK(mspi_nand_cmd_read_x1(0x00, (uint32_t *)page_buffer, PAGE_SIZE));

    // Read params page using SPI
    RET_CHECK(mspi_nand_read_params_page(params_page, PARAMETER_PAGE_SIZE, false));

    // Print out parameters page without details for debugging
    RET_CHECK(onfi_print(params_page, PARAMETER_PAGE_SIZE, false));

    // Read params page using Quad IO
    RET_CHECK(mspi_nand_read_params_page(quad_params_page, PARAMETER_PAGE_SIZE, true));

    // Check same
    if (memcmp(params_page, quad_params_page, PARAMETER_PAGE_SIZE) != 0) {
        am_util_stdio_printf("Flash TEST: Read x1 and Read Quad I/O get different parameter page! \n");
        return AM_HAL_STATUS_FAIL;
    }

    #undef RET_CHECK
    #undef TOSTRING
    #undef STRINGIFY 

    return AM_HAL_STATUS_SUCCESS;
}