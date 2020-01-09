/*
 * nand_flash.h
 * Driver for QuadSPI NAND Flash for Apollo 3 MCU
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

#define PAGE_SIZE       (2*1024)  
#define LOG2_PAGE_SIZE  11
#define PAGES_PER_BLOCK 64
#define LOG2_PPB        6
#define NUM_BLOCKS      2048

// TODO: Compile time check 2^log2_page_size == page_size

uint32_t nand_init(void **pHandle);

uint32_t nand_id(void);

uint32_t nand_reset(void);

uint32_t nand_read_params_page(uint8_t *params_page, uint32_t len, bool use_quad);

uint32_t nand_check_bad_block(uint32_t block_addr, bool *is_bad);

uint32_t nand_mark_bad_block(uint32_t block_addr);

uint32_t nand_erase_block(uint16_t block_addr);

uint32_t nand_prog_page(uint32_t page_addr, const uint8_t data[]);

uint32_t nand_is_free(uint32_t page_addr, bool *is_free);

uint32_t nand_read_page(uint32_t page, uint16_t offset, 
                             uint8_t *data, uint32_t len, 
                             bool *ecc_err);

uint32_t nand_copy_page(uint32_t src_page_addr, uint32_t dest_page_addr, 
                        bool *ecc_fatal);

/* After init, run this to test all features */
uint32_t nand_test(bool block_test, bool program_test);

/* Print all bad blocks, for debug */
uint32_t nand_print_bad_blocks(void);


#endif // AM_DEVICES_MSPI_FLASH_H

