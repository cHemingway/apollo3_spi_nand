// Tests for nand_flash.c using metal.test.unittest
// Runs as a main
// See https://github.com/klemens-morgenstern/metal.test/


#include <stdio.h> // For printf, will pick up _write() in SEGGER_RTT_Syscalls_GCC.c
#include <string.h> // memcmp

#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "system_apollo3.h"

#include "system.h"

#include "nand_flash.h"
#include "nand_flash_private.h"

#include "metal/unit.h"


// Buffer for entire page read/write test
// Align for DMA to word size, TODO: Is this needed?
uint8_t page_buffer[PAGE_SIZE] __attribute__((aligned(4)));

// Helper macro, check if AM_HAL_SUCCESS has been returned
#define ASSERT_SUCCESS(cond) METAL_ASSERT_EQUAL(cond,AM_HAL_STATUS_SUCCESS)

void test_nand_id() {
    ASSERT_SUCCESS(nand_id());
}

// Test if write enable bit can be set/unset
void test_writable() {
    bool writable;
    
    // Enable write and check writable bit
    ASSERT_SUCCESS(_nand_cmd_write_enable());
    ASSERT_SUCCESS(_nand_get_writable(&writable));
    METAL_ASSERT_EQUAL(writable,true);
    // Disable write and check if writable status is correct
    ASSERT_SUCCESS(_nand_cmd_write_disable());
    ASSERT_SUCCESS(_nand_get_writable(&writable));
    METAL_ASSERT_EQUAL(writable,false);
}


// Test reading a page into cache
void test_read_page() {
    bool busy;
    bool retcode1, retcode2;

    // Here we have a test that needs to be performed very quickly
    // As metal causes ~50mS delay, we need to save the return values and check later
    retcode1 = _nand_cmd_page_read(0xa5); // Read block a5 = 165, chosen for pattern
    retcode2 = _nand_get_busy(&busy);
    METAL_ASSERT_MESSAGE(retcode1==0, "nand_cmd_page_read returned ok");
    METAL_ASSERT_MESSAGE(retcode2==0, "nand_get_busy returned ok");
    METAL_ASSERT_EQUAL(busy, true);
    if (METAL_ERROR()) {
        METAL_LOG("Flash is not busy immediately after CMD_READ_PAGE! \n");
    }
    am_util_delay_us(80); // tRD is 80uS max with ECC enabled
    ASSERT_SUCCESS(_nand_get_busy(&busy));
    METAL_ASSERT_EQUAL(busy, false);
    if (METAL_ERROR()) {
        METAL_LOG("Flash is still busy >80uS after CMD_READ_PAGE! \n");
    }
}

// Test writing a page into cache and reading it back. Do not actually program.
void test_write_read_cache() {
    // Generate ascending bytes
    for (int i=0;i<PAGE_SIZE;i++) {page_buffer[i] =i&0xff;} // Ascending bytes
    // Program and read back
    ASSERT_SUCCESS(_nand_cmd_program_load_x1(0, (uint32_t *)page_buffer, PAGE_SIZE));
    ASSERT_SUCCESS(_nand_cmd_read_x1(0, (uint32_t *)page_buffer, PAGE_SIZE));
    
    // Wrap compare in critical, as we don't want to keep displaying wrong values
    METAL_ENTER_CRITICAL();
    // Compare using METAL_RANGED
    // Removed! Takes too long as prints every value via GDB
    //METAL_RANGED(__metal_level_assert, 
    //            page_buffer, PAGE_SIZE, i, PAGE_SIZE, 
    //            METAL_ASSERT_EQUAL(page_buffer[i], (i&0xff)));

    METAL_LOG("Comparing read vs written");
    for (int i=0; i<PAGE_SIZE; i++) {
        if (page_buffer[i] != (i & 0xff)) {
            METAL_ASSERT(page_buffer[i] == (i & 0xff));
        }
    }
    METAL_EXIT_CRITICAL();
}


int main(void)
{
    uint32_t retcode;
    void *pHandle = NULL;

    system_init();
    am_hal_gpio_output_set(AM_BSP_GPIO_LED0);


    // Init nand, failure is critical
    retcode = nand_init(&pHandle);
    METAL_CRITICAL(
        METAL_ASSERT_EQUAL(retcode, AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS));

    // Call test suites
    METAL_CALL(&test_nand_id, "Test nand_id");
    METAL_CALL(&test_writable, "Test writable");
    METAL_CALL(&test_read_page, "Read page");
    METAL_CALL(test_write_read_cache, "Test write/read cache");

    am_hal_gpio_output_clear(AM_BSP_GPIO_LED0);
    return METAL_REPORT();
}


void _exit(int code) {
    am_hal_gpio_output_set(AM_BSP_GPIO_LED1);
    while(1);
}