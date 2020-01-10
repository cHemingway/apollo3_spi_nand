// Template project for Ambiq Apollo 3, including Segger RTT and SysTick example
// Flashes LED on Sparkfun Edge board


#include <stdio.h> // For printf, will pick up _write() in SEGGER_RTT_Syscalls_GCC.c
#include <string.h> // memcmp
#include <stdlib.h> // srand, rand

#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "system_apollo3.h"

#include "system.h"
#include "nand_flash.h"

#include "dhara/map.h"
#include "dhara/error.h"
#include "dhara_adaptor.h"

#ifndef TEST_BLOCK
#define TEST_BLOCK      false
#endif

#ifndef TEST_PROGRAM
#define TEST_PROGRAM    true
#endif

uint8_t dhara_page_buf[PAGE_SIZE];
uint8_t dhara_test_buf[PAGE_SIZE];




// Forked versions of dhara/tests/util.c to not use abort()
void seq_gen(unsigned int seed, uint8_t *buf, size_t length)
{
	size_t i;
	srand(seed);
	for (i = 0; i < length; i++)
		buf[i] = rand();
}

void seq_assert(unsigned int seed, const uint8_t *buf, size_t length)
{
	size_t i;

	srand(seed);
	for (i = 0; i < length; i++) {
		const uint8_t expect = rand();

		if (buf[i] != expect) {
			fprintf(stderr, "seq_assert: mismatch at %d in "
				"sequence %d: 0x%02x (expected 0x%02x)\n",
				i, seed, buf[i], expect);
			while(1); // Changed from abort()
		}
	}
}


//*****************************************************************************
// Main
//*****************************************************************************
int main(void)
{

    uint32_t      retcode;
    void          *pHandle = NULL;

    system_init();

    printf("Starting Flash Tests \n");                  // Outputs over Segger RTT

    // Init flash and check was OK
    retcode = nand_init(&pHandle);
    if (AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS != retcode)
    {
        printf("Failed to configure the MSPI and Flash Device correctly!\n");
        am_hal_gpio_state_write(AM_BSP_GPIO_LED0, AM_HAL_GPIO_OUTPUT_SET);
    }

    // DHARA FS Map Tests
    // From dhara/tests/journal.c
    {
        printf("Running dhara tests.. \n");
        struct dhara_map map;
        dhara_error_t dhara_err;
        // Number of sectors to test. Too many makes logic analyser decode crash
        const unsigned test_sectors = 3;   

        printf("Map init\n");
        dhara_map_init(&map, &dhara_adaptor_nand, dhara_page_buf, GC_RATIO);
        dhara_map_resume(&map, NULL);
        printf("  capacity: %ld\n", dhara_map_capacity(&map));
        printf("\n");

        printf("Writing %u sectors \n", test_sectors);
        for (int i=0; i<test_sectors; i++) {
            seq_gen(i, dhara_test_buf, PAGE_SIZE);
            if (dhara_map_write(&map, i, dhara_test_buf, &dhara_err) < 0) {
		        printf("map_write error: %s \n", dhara_strerror(dhara_err));
                return 1; // TODO exit()
            }
        }

        printf("Sync...\n");
        retcode = dhara_map_sync(&map, &dhara_err);
        if (retcode) {
            printf("dhara_map_sync error %s \n",dhara_strerror(dhara_err));
        }
        printf("Resume...\n");
        dhara_map_init(&map, &dhara_adaptor_nand, dhara_page_buf, GC_RATIO);
        retcode = dhara_map_resume(&map, &dhara_err);
        if (retcode) {
            printf("dhara_map_resume error %s \n",dhara_strerror(dhara_err));
        }
        printf("  capacity: %ld\n", dhara_map_capacity(&map));
	    printf("  use count: %ld\n", dhara_map_size(&map));

        printf("Read back...\n");
	    for (int i = 0; i < test_sectors; i++) {
		    
            if(dhara_map_read(&map, i, dhara_test_buf, &dhara_err)) {
                printf("map_read error: %s \n", dhara_strerror(dhara_err));
                return 1; // TODO exit()
            }
		    seq_assert(i, dhara_test_buf, PAGE_SIZE);
        }
        printf("Success! /n");
        while(1);
	}


    // Go to Deep Sleep.
    am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
    
}
