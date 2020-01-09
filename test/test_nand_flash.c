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
#include "onfi_print.h"

#include "metal/unit.h"


// Buffer for entire page read/write test
// Align for DMA to word size, TODO: Is this needed?
uint8_t page_buffer[PAGE_SIZE] __attribute__((aligned(4)));

// Test buffer to store pages in
uint8_t write_page[PAGE_SIZE];
uint8_t read_page[PAGE_SIZE];

#define PARAMETER_PAGE_SIZE 256 //TODO: Expose via nand_flash.h
uint8_t params_page[PARAMETER_PAGE_SIZE];

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


void test_first_block_good(void) {
    bool is_bad;
    ASSERT_SUCCESS(nand_check_bad_block(0, &is_bad));
    METAL_ASSERT_EQUAL(is_bad, false);
}


void test_params_page(void) {
    ASSERT_SUCCESS(nand_read_params_page(params_page, PARAMETER_PAGE_SIZE, false));
    ASSERT_SUCCESS(onfi_print(params_page, PARAMETER_PAGE_SIZE, false));
}


void test_1st_page_free(void) {
    bool is_free;
    ASSERT_SUCCESS(nand_is_free(0, &is_free));
    // Might have been used, so should only be expect
    METAL_EXPECT_EQUAL(is_free, true);          
}


void test_block_marking(void) {
        bool is_bad;
        int marked_block = 5; // Mark block 5
        
        // Erase block before marking
        ASSERT_SUCCESS(nand_erase_block(marked_block));

        // Should be good
        ASSERT_SUCCESS(nand_check_bad_block(marked_block, &is_bad));
        METAL_ASSERT_EQUAL(is_bad, false);
        // Mark the block and check it is marked correctly
        ASSERT_SUCCESS(nand_mark_bad_block(marked_block));
        ASSERT_SUCCESS(nand_check_bad_block(marked_block, &is_bad));
        METAL_ASSERT_EQUAL(is_bad, true);
        METAL_LOG("Checking other blocks are not marked");
        for (int i=0; i<8; i++) {
            ASSERT_SUCCESS(nand_check_bad_block(i, &is_bad));
            printf("%d ",i);
            if (is_bad == true) {
                // Check all other blocks are marked good
                METAL_ASSERT_NOT_EQUAL(i, marked_block);
            }
        }
}


void test_program(void) {
        const uint32_t block_addr = 4;   // Block 4 is in gauranteed non-bad section
        const uint32_t page_addr = block_addr*PAGES_PER_BLOCK;
        bool ecc_err;

        // Erase page and write value
        ASSERT_SUCCESS(nand_erase_block(block_addr));

        // Check is marked as free now it has been erased
        bool is_free;
        ASSERT_SUCCESS(nand_is_free(page_addr, &is_free));
        METAL_ASSERT(is_free);

        // Generate Data and write to page
        for (int i=0; i<PAGE_SIZE; i++) {
            write_page[i] = ((i&0xf) << 4) | (i&0xf); // generate 00, 11, 22, 33 etc
        }
        ASSERT_SUCCESS(nand_prog_page(page_addr, write_page));


        // Read page 0 in between to clear cache
        ASSERT_SUCCESS(nand_read_page(0, 0, read_page, PAGE_SIZE, &ecc_err));

        // Read back
        ASSERT_SUCCESS(nand_read_page(page_addr, 0, read_page, PAGE_SIZE, &ecc_err));

        // Check read correctly
        if(0 != memcmp(read_page, write_page, PAGE_SIZE)) {
            METAL_ASSERT_MESSAGE(0,"Read vs Written does not match.");
        }

        // Check different offsets read 32 bytes correctly
        METAL_ENTER_CRITICAL();
        for (int offset=1; offset< (2048 - 32); offset*=2) {
            ASSERT_SUCCESS(nand_read_page(page_addr, offset, read_page, 32, &ecc_err));
            for (int j=0; j<32; j++) {
                uint32_t i = offset + j;    // Calculate value
                uint8_t expected = ((i&0xf) << 4) | (i&0xf);
                // Compare against expected. We use j as this is without read offset
                if (expected != read_page[j] ) { 
                    // TODO: Show offset somehow in metal message?
                    METAL_ASSERT_MESSAGE(0,"Error, read does not match written");   
                }
            }
        }
        METAL_EXIT_CRITICAL();
        

        // Check is not marked as free now it has been written to
        ASSERT_SUCCESS(nand_is_free(page_addr, &is_free));
        METAL_ASSERT(!is_free);

        // Copy to next page, since the block has been erased
        bool ecc_fatal;
        ASSERT_SUCCESS(nand_copy_page(page_addr, page_addr+1, &ecc_fatal));
        METAL_ASSERT(!ecc_fatal);

        // Read back from page+1 to check copy_page worked
        ASSERT_SUCCESS(nand_read_page(page_addr+1, 0, read_page, PAGE_SIZE, &ecc_err));

        // Check read correctly
        if(0 != memcmp(read_page, write_page, PAGE_SIZE)) {
            METAL_ASSERT_MESSAGE(0,"Read vs Written matches");
        }

        // TODO: Clean up by erasing blocks
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
    METAL_CALL(test_nand_id, "Test nand_id");
    METAL_CALL(test_writable, "Test writable");
    METAL_CALL(test_read_page, "Read page");
    METAL_CALL(test_write_read_cache, "Test write/read cache");
    METAL_CALL(test_first_block_good, "Test first block marked good");
    METAL_CALL(test_params_page, "Test reading params page");
    METAL_CALL(test_1st_page_free, "Test 1st page marked free");
    METAL_CALL(test_block_marking, "Test marking a block bad");
    METAL_CALL(test_program, "Test program and copy page operations");

    am_hal_gpio_output_clear(AM_BSP_GPIO_LED0);
    return METAL_REPORT();
}


void _exit(int code) {
    // Hooked by metal.test to ensure it knows when tests done
    am_hal_gpio_output_set(AM_BSP_GPIO_LED1);
    while(1);
}