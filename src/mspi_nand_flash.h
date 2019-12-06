/*
 * mspi_nand_flash.h, 
 * Chris Hemingway 2019
 */

#ifndef AM_DEVICES_MSPI_FLASH_H
#define AM_DEVICES_MSPI_FLASH_H

#include <stdint.h>
#include "am_bsp.h"

typedef enum
{
    AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS,
    AM_DEVICES_MSPI_FLASH_STATUS_ERROR
} am_devices_mspi_flash_status_t;


uint32_t mspi_nand_flash_init(void **pHandle);

uint32_t mspi_nand_flash_id(void);

uint32_t mspi_nand_flash_reset(void);


#endif // AM_DEVICES_MSPI_FLASH_H

