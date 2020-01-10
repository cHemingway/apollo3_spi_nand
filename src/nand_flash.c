/*
 * nand_flash.h
 * Driver for QuadSPI NAND Flash for Apollo 3 MCU
 * Copyright: Chris Hemingway, 2019
 */

//*****************************************************************************
//
// Functions nand_init, am_device_command_write, am_device_command_read 
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

#include "nand_flash.h"
#include "nand_flash_private.h"
#include "onfi_print.h"

#include "am_util_stdio.h"
#include "am_util_delay.h"
#include "am_bsp.h"

#ifdef MICRON_MT29F8G01AD
#include "mt79f_cmd_regs.h"           // Command and register values, page size
#endif

#if DEVICE_PAGE_SIZE != PAGE_SIZE
#error "PAGE_SIZE in nand_flash.h is not equal to device page size"
#endif

// Disable warning for unused functions in this file
#pragma GCC diagnostic ignored "-Wunused-function"


// Timeout for MSPI HAL read/write acesses in microseconds
// Set to 10mS as flash should respond immediately
#define AM_DEVICES_MSPI_FLASH_TIMEOUT             10000

 // Time to wait in uS between polling the flash device status reg
 // Should be ~10x larger than execution time to ensure 90% accurate timing.
 // HAL has ~1uS delay minimum (due to polling loop), so 10uS sounds good
const uint32_t POLL_DELAY_US =                    10;

#ifdef MICRON_MT29F8G01AD


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
    .ui8WriteInstr        = CMD_PROGRAM_LOAD_RANDOM,
    .ui32TCBSize          = 0,                              // No DMA Transfer Control Buffer
    .pTCB                 = NULL,
    .scramblingStartAddr  = 0,                              // No data scrambling
    .scramblingEndAddr    = 0,
};

#else // Not defined flash ID
#error "No Flash defined!"
#endif


typedef enum {
    ECC_OK = 0,
    ECC_CORRECTED,      // Low level of error, refreshment not needed
    ECC_SHOULD_REFRESH, // Medium error, should refresh
    ECC_MUST_REFRESH,   // High level of error, must refresh data
    ECC_FATAL           // Fatal error, cannot recover data fully
} ecc_err_t;


// Pointer to MSPI peripheral
void                            *g_pMSPIHandle;   

// Transaction state
am_hal_mspi_pio_transfer_t      g_PIOTransaction;


const uint32_t ui32Module = 0; // Index of MSPI module. Apollo3 only has MSPI0

// Forward definition
uint32_t nand_init_device(void);

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

        g_PIOTransaction.bQuadCmd         = false;
    g_PIOTransaction.pui32Buffer        = pData;

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);

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
        g_PIOTransaction.ui32NumBytes     = ui32NumBytes;
        g_PIOTransaction.bQuadCmd      = false;

    g_PIOTransaction.pui32Buffer        = pData;

    // Execute the transction over MSPI.
    ui32Status = am_hal_mspi_blocking_transfer(g_pMSPIHandle, &g_PIOTransaction,
                                         AM_DEVICES_MSPI_FLASH_TIMEOUT);

    return ui32Status;
}



uint32_t nand_reset(void)
{

  if (AM_HAL_STATUS_SUCCESS != am_device_command_write(ui32Module, CMD_RESET, false, 0, NULL, 0))
  {
    return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
  }
  am_util_delay_ms(RESET_TIME_MS);

  return AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS;
}



uint32_t nand_init(void **pHandle)
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
        


    if (AM_HAL_STATUS_SUCCESS != nand_reset())
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }

    // Device specific MSPI Flash initialization.
    ui32Status = nand_init_device();
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_FLASH_STATUS_ERROR;
    }

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

    // HACK: Set lower drive strength for clock to avoid ringing
    // Ideally this should be within BSP
    // Copy of g_AM_BSP_GPIO_MSPI_SCK with lower drive strength
    {
        am_hal_gpio_pincfg_t GPIO_MSPI_SCK = g_AM_BSP_GPIO_MSPI_SCK;
        am_hal_gpio_pincfg_t GPIO_MSPI_D1 = g_AM_BSP_GPIO_MSPI_D1;
        am_hal_gpio_pincfg_t GPIO_MSPI_CE0 = g_AM_BSP_GPIO_MSPI_CE0;
        GPIO_MSPI_SCK.eDriveStrength = AM_HAL_GPIO_PIN_DRIVESTRENGTH_4MA; // Reduce 12 to 4
        GPIO_MSPI_D1.eDriveStrength = AM_HAL_GPIO_PIN_DRIVESTRENGTH_4MA; // Reduce 8 to 4
        GPIO_MSPI_CE0.eDriveStrength = AM_HAL_GPIO_PIN_DRIVESTRENGTH_4MA; // Reduce 12 to 4
        am_hal_gpio_pinconfig(AM_BSP_GPIO_MSPI_SCK, GPIO_MSPI_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_MSPI_D1, GPIO_MSPI_D1);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_MSPI_CE0, GPIO_MSPI_CE0);
    }


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
uint32_t mspi_set_use_quadspi(bool use_quadspi) {
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


uint32_t nand_id(void)
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
uint32_t _nand_cmd_get_features(uint8_t addr, uint8_t *data) {

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
uint32_t _nand_cmd_set_features(uint8_t addr, uint8_t data) {

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


uint32_t _nand_cmd_write_enable(void) {
    uint32_t      ui32Status;

    // Send write_enable command, no address, no data
    ui32Status = am_device_command_write(ui32Module, CMD_WRITE_ENABLE, false, 0, NULL, 0);
    return ui32Status;
}


uint32_t _nand_cmd_write_disable(void) {
    uint32_t      ui32Status;

    // Send write_enable command, no address, no data
    ui32Status = am_device_command_write(ui32Module, CMD_WRITE_DISABLE, false, 0, NULL, 0);
    return ui32Status;
}


uint32_t _nand_get_writable(bool *writable) {
    uint32_t    ui32Status;
    uint8_t    status_reg;

    // Get the status register
    ui32Status = _nand_cmd_get_features(FEATURE_REG_STATUS, &status_reg);
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

/*
 * Check if NAND is currently busy, returns immediately
 */
uint32_t _nand_get_busy(bool *busy) {
    uint32_t    ui32Status;
    uint8_t    status_reg;

    // Get the status register
    ui32Status = _nand_cmd_get_features(FEATURE_REG_STATUS, &status_reg);
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
 * Wait until NAND is no longer busy, indicates if program or erase failure occured
 * status is pointer to raw status register, e.g. for checking ECC bits
 */
uint32_t nand_wait_busy(uint32_t timeout_us, bool *program_fail, bool *erase_fail,
                               uint8_t *status) {
    bool        busy;
    uint32_t    ui32Status;
    uint8_t    status_reg;
    int32_t    timeout_count = timeout_us; // Convert to integer so can go negative

    do {
        // Get the status register
        ui32Status = _nand_cmd_get_features(FEATURE_REG_STATUS, &status_reg);
        if (ui32Status != AM_HAL_STATUS_SUCCESS) { // Exit early on error
            return ui32Status;
        }
        // Get busy flag
        busy = (status_reg & FEATURE_REG_STATUS_OIP_MASK) != 0;
        if (!busy) {
            break;      // Exit loop early so we don't have delay if busy==false
        }
        am_util_delay_us(POLL_DELAY_US);
        timeout_count -= POLL_DELAY_US;
    } while (timeout_count>0);

    // Check if we timed out
    if (busy & (timeout_count <= 0)) {
        return AM_HAL_STATUS_TIMEOUT;
    }

    // Check bits
    *program_fail = (status_reg & FEATURE_REG_STATUS_P_FAIL_MASK) != 0;
    *erase_fail = (status_reg & FEATURE_REG_STATUS_E_FAIL_MASK) != 0;

    // Return raw status reg
    if (status != NULL) {
        *status = status_reg;
    }

    return ui32Status;
}


/* 
 * Execute PAGE_READ command to read a page into the cache given page address
 */
uint32_t _nand_cmd_page_read(uint32_t page_addr) {
    uint32_t ui32Status;

    // Change to 3 byte addresses 
    MSPI->CFG_b.ASIZE = 0x02;   // Address is 3 bytes
    ui32Status = am_device_command_write(ui32Module, CMD_PAGE_READ, true, page_addr, NULL, 0);
    return ui32Status;
}


/* 
 * Execute READ FROM CACHE x1 to read single page into Cache
 */
uint32_t _nand_cmd_read_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
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
uint32_t _nand_cmd_read_x4(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
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
uint32_t _nand_cmd_read_quadio(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
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
uint32_t _nand_cmd_program_load_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
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
uint32_t _nand_cmd_program_load_random_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len) {
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
uint32_t _nand_cmd_program_execute(uint32_t page_addr) {
    uint32_t ui32Status;

    // Change to 3 byte addresses 
    MSPI->CFG_b.ASIZE = 0x02;   // Address is 3 bytes
    ui32Status = am_device_command_write(ui32Module, CMD_PROGRAM_EXECUTE, true, page_addr, NULL, 0);
    return ui32Status;
}


/*
 * Execute BLOCK_ERASE to erase an entire block
 */
uint32_t _nand_cmd_block_erase(uint32_t page_addr) {
    uint32_t ui32Status;

    // Change to 3 byte addresses 
    MSPI->CFG_b.ASIZE = 0x02;   // Address is 3 bytes
    ui32Status = am_device_command_write(ui32Module, CMD_BLOCK_ERASE, true, page_addr, NULL, 0);
    return ui32Status;
}


// Get ECC bits from status register
static ecc_err_t nand_status_to_ecc(uint8_t status_reg) {
    // Decode status reg into ECC error count, device specific
    #ifdef MICRON_MT29F8G01AD
    switch ((status_reg & FEATURE_REG_STATUS_ECC_MASK) >> FEATURE_REG_STATUS_ECC_SHIFT)
    {
        case FEATURE_REG_STATUS_ECC_NO_ERR:
            return ECC_OK;
        case FEATURE_REG_STATUS_ECC_1_3_ERR:
            return ECC_CORRECTED;
        case FEATURE_REG_STATUS_ECC_4_6_ERR:
            return ECC_SHOULD_REFRESH;
        case FEATURE_REG_STATUS_ECC_7_8_ERR:
            return ECC_MUST_REFRESH;
        case FEATURE_REG_STATUS_ECC_FATAL_ERR:
            return ECC_FATAL;
        default: // Should never happen! Safest to assume data is corrupted
            return ECC_FATAL;
    }
    #endif
}

/*
 * Device specific initialization commands
 */
uint32_t nand_init_device(void) {
    // Unlock all blocks, as all are locked by default after power up
    return _nand_cmd_set_features(FEATURE_REG_BLOCK_LOCK, FEATURE_REG_BLOCK_LOCK_UNLOCK_ALL);
}


/*
 * Erase a block and checks status returned by chip
 */
uint32_t nand_erase_block(uint16_t block_addr) {
    uint32_t ui32Status;
    bool unused, erase_err;
    // Enable write, as gets cleared by last program or erase_block function
    ui32Status = _nand_cmd_write_enable();
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Block erase command
    ui32Status = _nand_cmd_block_erase(block_to_page_addr(block_addr));
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Wait until not busy, check erase err, but ignore program error
    ui32Status = nand_wait_busy(ERASE_TIME_US, &unused, &erase_err, NULL);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    if (erase_err) return AM_HAL_STATUS_HW_ERR;
    return AM_HAL_STATUS_SUCCESS;
}


/*
 * Function to program a page. Takes data[] of PAGE_SIZE and programs the lot
 * Caution: Does not check if block is bad or not!
 */
uint32_t nand_prog_page(uint32_t page_addr, const uint8_t data[]) {
    bool program_fail, erase_fail;
    uint32_t ui32Status;

    // Write enable must be sent before program load and program execute
    ui32Status = _nand_cmd_write_enable();
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Load marker value into our byte offset, within spare area
    ui32Status = _nand_cmd_program_load_x1(0, (uint32_t *)data, PAGE_SIZE);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Execute the write, write enable must be sent just before!
    ui32Status = _nand_cmd_write_enable();
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    ui32Status = _nand_cmd_program_execute(page_addr);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Wait for write to be completed, or error
    ui32Status = nand_wait_busy(PROGRAM_TIME_US, &program_fail, &erase_fail, NULL);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    if (program_fail) return AM_HAL_STATUS_HW_ERR;

    return AM_HAL_STATUS_SUCCESS; // Success
}


/*
 * Check that the given page has been erased. 
 * Sets is_free to true if so, false if not
 */
uint32_t nand_is_free(uint32_t page_addr, bool *is_free) {
    uint32_t ui32Status;

    uint8_t ecc_data[ECC_AREA_LENGTH]; 
    
    /* We check if a page is free by reading the ECC data from the main/spare area
     * If it is all 0xFF, then it is extremely likely that the page has not been programmed
     * The chance of it all being 0xFF by coincidence is 1 / 2^(ECC_AREA_LENGTH)
     * TODO: Check this does not occur for all zero data
     */

    // Don't check ECC in status reg on read, as the ECC bits are not themselves ECC protected
    ui32Status = nand_read_page(page_addr, ECC_AREA_OFFSET, ecc_data, 
                                ECC_AREA_LENGTH, NULL);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    if (is_free != NULL) {
        *is_free = true;
        // Check if free by testing every byte against unprogrammed value
        // We could speed this up by checking 32 bits at a time
        for(int i=0; i<ECC_AREA_LENGTH; i++) {
            if (ecc_data[i] != ECC_UNPROGRAMMED_VALUE) {
                *is_free = false;
            }
        }
    } 
    // Success
    return AM_HAL_STATUS_SUCCESS;
}


/*
 * Read a portion of a page
 * Sets ecc_fatal if an uncorrectable ECC error occurred (correctable is ignored)
 */
uint32_t nand_read_page(uint32_t page_addr, uint16_t offset, 
                             uint8_t *data, uint32_t len, 
                             bool *ecc_fatal) {
    uint32_t ui32Status;
    bool ignore1, ignore2;
    uint8_t status_reg;
    ecc_err_t ecc_err;

    // Issue page read command
    ui32Status = _nand_cmd_page_read(page_addr);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Wait for completion
    ui32Status = nand_wait_busy(PAGE_READ_TIME_US, &ignore1, &ignore2, &status_reg);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Check ECC error. TODO: ECC error counters
    ecc_err = nand_status_to_ecc(status_reg);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    if (ecc_fatal != NULL) {
    *ecc_fatal = (ecc_err == ECC_FATAL);
    }
    // Read out the data at offset
    ui32Status = _nand_cmd_read_x1(offset, (uint32_t *)data, len);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Success
    return AM_HAL_STATUS_SUCCESS;
}


/*
 * Copy a page from one address to another.
 * Takes source and destination address, and detects if an ECC error occured.
 * Uses flash devices internal cache for speed.
 */
uint32_t nand_copy_page(uint32_t src_page_addr, uint32_t dest_page_addr, bool *ecc_fatal) {
    uint32_t ui32Status;
    bool unused1, unused2;
    uint8_t status_reg;
    ecc_err_t ecc_err;

    /*
     * Following INTERNAL_DATA_MOVE sequence of Micron MT79A
     */
    ui32Status = _nand_cmd_page_read(src_page_addr);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Not mentioned in datasheet if checking status reg is needed here
    // However otherwise, we would not get ECC status
    // TODO: Check status reg at end for ECC errors
    ui32Status = nand_wait_busy(PAGE_READ_TIME_US, &unused1, &unused2, &status_reg);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    ecc_err = nand_status_to_ecc(status_reg);
    if ((ecc_err == ECC_FATAL) && (ecc_fatal != NULL)) {
        *ecc_fatal = true;
        return AM_HAL_STATUS_HW_ERR; // Exit early on ECC fail
    }
    // Enable write, must be called before program ops
    ui32Status = _nand_cmd_write_enable();
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // PROGRAM_LOAD_RANDOM_DATA with no data, bit unclear if nescessary
    ui32Status = _nand_cmd_program_load_random_x1(0, NULL, 0);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Finally, write out the page
    ui32Status = _nand_cmd_program_execute(dest_page_addr);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    ui32Status = nand_wait_busy(PROGRAM_TIME_US, &unused1, &unused2, &status_reg);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Success!
    return AM_HAL_STATUS_SUCCESS;
}


/*
 * Read the parameter page (blocking) from the flash into *params_page of size len
 * Len must be > PARAMETER_PAGE_SIZE
 */
uint32_t nand_read_params_page(uint8_t *params_page, uint32_t len, bool use_quad) {
    uint32_t ui32Status;
    uint8_t  reg_config, old_reg_config;

    // Check length parameter
    if (len < PARAMETER_PAGE_SIZE) {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    // Modify CFG bits of Feature register Configuration (0xB0) to get params page
    ui32Status = _nand_cmd_get_features(FEATURE_REG_CONFIG, &old_reg_config);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    reg_config = old_reg_config & ~FEATURE_REG_CONFIG_CFG_MASK; // Clear CFG bits
    reg_config |= FEATURE_REG_CONFIG_CFG_VALUE_READ_PARAMS;     // Set CFG to VALUE_READ_PARAMS, 010

    ui32Status = _nand_cmd_set_features(FEATURE_REG_CONFIG, reg_config);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Read parameter page. ECC is not used for this page, see datasheet.
    ui32Status = nand_read_page(PARAMETER_PAGE_PAGE_ADDR, PARAMETER_PAGE_COLUMN_ADDR,
                   params_page, len, NULL); 
        if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Write original value back to old_reg_config to exit parameter page reading mode
    ui32Status = _nand_cmd_set_features(FEATURE_REG_CONFIG, old_reg_config);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    return AM_HAL_STATUS_SUCCESS; // Return success
}


uint32_t nand_check_bad_block(uint32_t block_addr, bool *is_bad) {
    uint32_t ui32Status, page_addr;
    uint8_t markers[BAD_BLOCK_AREA_LENGTH];

    // Convert block addr into page addr (get first page)
    page_addr = block_to_page_addr(block_addr);

    // Read two bytes at marker address of first byte. One factory, one ours
    // We ignore ECC as won't be set for a bad block
    ui32Status = nand_read_page(page_addr + BAD_BLOCK_PAGE_OFFSET, 
                                BAD_BLOCK_AREA_OFFSET,
                                markers, BAD_BLOCK_AREA_LENGTH, NULL); 
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;

    // Check for bad block markers
    *is_bad = ((markers[BAD_BLOCK_OUR_BYTE_OFFSET]     == BAD_BLOCK_MARKER_VALUE) || 
               (markers[BAD_BLOCK_FACTORY_BYTE_OFFSET] == BAD_BLOCK_MARKER_VALUE));
    // Success
    return ui32Status;
}


/*
 * Function to mark a block as bad, takes block (not page) address
 * Requires write to previously be enabled, and the spare location to equal 0xff
 * Caution: Not intended to be reversible!
 */
uint32_t nand_mark_bad_block(uint32_t block_addr) {
    bool busy = false, program_fail, erase_fail;
    uint32_t ui32Status, page_addr;
    uint8_t marker_value[] = {BAD_BLOCK_MARKER_VALUE}; // Needs to be array

    /*
     * We mark a block as bad by writing into our own
     * To do this without loosing all the page data (useful for recovery?) we
     * - Load the page into the cache register
     * - Overwrite only our byte of the cache using program_load_random
     * - Execute the write using program execute
     * - Check completed, and OK (no write error)
     */
    page_addr = block_to_page_addr(block_addr);
    // Read page into cache
    ui32Status = _nand_cmd_page_read(page_addr);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Wait until read completed
    do {
        ui32Status = _nand_get_busy(&busy);
        if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    } while(busy);

    // Execute the write, write enable must be sent before program load
    ui32Status = _nand_cmd_write_enable();
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Load marker value into our byte offset, within spare area
    ui32Status = _nand_cmd_program_load_random_x1(BAD_BLOCK_AREA_OFFSET + BAD_BLOCK_OUR_BYTE_OFFSET,
                                                      (uint32_t *)marker_value,
                                                      1);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    ui32Status = _nand_cmd_program_execute(page_addr);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    // Wait for write to be completed, or error
    ui32Status = nand_wait_busy(PROGRAM_TIME_US, &program_fail, &erase_fail, NULL);
    if (ui32Status != AM_HAL_STATUS_SUCCESS) return ui32Status;
    if (program_fail) return AM_HAL_STATUS_HW_ERR;

    return AM_HAL_STATUS_SUCCESS; // Success
}


/*
 * Utility function to print the address of all bad blocks
 * Can take a few hundred ms or so
 * FIXME: Why are we not finding any bad blocks?
 */
uint32_t nand_print_bad_blocks(void) {
    bool is_bad;
    uint32_t ui32Status, total = 0;

    am_util_stdio_printf("Checking for Bad Blocks, addresses (decimal): \n");
    for (uint32_t i=0; i<NUM_BLOCKS; i++) {
        ui32Status = nand_check_bad_block(i, &is_bad);
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