/*
 * mspi_nand_flash.h
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


uint32_t mspi_nand_init(void **pHandle);

uint32_t mspi_nand_id(void);

uint32_t mspi_nand_reset(void);

uint32_t mspi_nand_write_enable(void);

uint32_t mspi_nand_write_disable(void);

uint32_t mspi_nand_get_writable(bool *writable);

uint32_t mspi_nand_read_params_page(uint8_t *params_page, uint32_t len, bool use_quad);

/* After init, run this to test all features */
uint32_t mspi_nand_test(void);


#endif // AM_DEVICES_MSPI_FLASH_H

