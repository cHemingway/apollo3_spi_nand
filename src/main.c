// Template project for Ambiq Apollo 3, including Segger RTT and SysTick example
// Flashes LED on Sparkfun Edge board


#include <stdio.h> // For printf, will pick up _write() in SEGGER_RTT_Syscalls_GCC.c
#include <string.h> // memcmp

#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "system_apollo3.h"

#include "SEGGER_RTT.h"

#include "nand_flash.h"

#include "dhara/map.h"
#include "dhara/error.h"
#include "dhara_adaptor.h"

#ifndef TEST_BLOCK
#define TEST_BLOCK      true
#endif

#ifndef TEST_PROGRAM
#define TEST_PROGRAM    true
#endif

uint8_t dhara_page_buf[PAGE_SIZE];
uint8_t dhara_test_buf[PAGE_SIZE];

// Override am_print_string so hard_fault_handler outputs over RTT
void am_print_string(char *pcStr) {
    SEGGER_RTT_WriteString(0, pcStr);
}


// Increment g_tick_ms every systick
static volatile uint32_t g_tick_ms;  // Systick counter
void SysTick_Handler(void)
{
    g_tick_ms++;
}


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
			fprintf(stderr, "seq_assert: mismatch at %ld in "
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

    // Set the clock frequency, 48MHz, no turbo
    am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_SYSCLK_MAX, 0);

    // Set the default cache configuration
    am_hal_cachectrl_config(&am_hal_cachectrl_defaults);
    am_hal_cachectrl_enable();

    // Configure the board for low power operation.
    am_bsp_low_power_init();

    // Init RTT, printf() will be redirected to this
    SEGGER_RTT_Init(); 

    // Redirect AMBIQ Printf to RTT
    am_util_stdio_printf_init(am_print_string);

    // Enable interrupts.
    am_hal_interrupt_master_enable();

    SystemCoreClockUpdate();                //update clock variable SystemCoreClock (defined by CMSIS)
    SysTick_Config(SystemCoreClock / 1000); //setup 1ms SysTick (defined by CMSIS)

    printf("Starting Flash Tests \n");                  // Outputs over Segger RTT

    // Init flash and check was OK
    retcode = nand_init(&pHandle);
    if (AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS != retcode)
    {
        printf("Failed to configure the MSPI and Flash Device correctly!\n");
        am_hal_gpio_state_write(AM_BSP_GPIO_LED0, AM_HAL_GPIO_OUTPUT_SET);
    }

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

    // DHARA FS Map Tests
    // From dhara/tests/journal.c
    #if TEST_DHARA
    {
        printf("Running dhara tests.. \n");
        struct dhara_map map;
        dhara_error_t dhara_err;
        // Number of sectors to test. Too many makes logic analyser decode crash
        const unsigned test_sectors = 3;   

        printf("Map init\n");
        dhara_map_init(&map, &dhara_adaptor_nand, dhara_page_buf, GC_RATIO);
        dhara_map_resume(&map, NULL);
        printf("  capacity: %d\n", dhara_map_capacity(&map));
        printf("\n");

        printf("Writing %u sectors \n", test_sectors);
        for (int i=0; i<test_sectors; i++) {
            seq_gen(i, dhara_test_buf, PAGE_SIZE);
            if (dhara_map_write(&map, i, dhara_test_buf, &dhara_err) < 0) {
		        printf("map_write error: %s \n", dhara_strerror(dhara_err));
                return; // TODO exit()
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
        printf("  capacity: %d\n", dhara_map_capacity(&map));
	    printf("  use count: %d\n", dhara_map_size(&map));

        printf("Read back...\n");
	    for (int i = 0; i < test_sectors; i++) {

            if(dhara_map_read(&map, i, dhara_test_buf, &dhara_err)) {
                printf("map_read error: %s \n", dhara_strerror(dhara_err));
                return; // TODO exit()
            }
		    seq_assert(i, dhara_test_buf, PAGE_SIZE);
        }
        printf("Success! /n");
        while(1);
    }
    #endif

    

    while (1) {
        retcode = nand_test(TEST_BLOCK, TEST_PROGRAM);
        if (retcode == AM_HAL_STATUS_SUCCESS) {
            printf("FLASH TEST PASS \n\n");
            am_hal_gpio_output_set(AM_BSP_GPIO_LED0);
            am_hal_gpio_output_clear(AM_BSP_GPIO_LED3);
        } else { // Error
            printf("FLASH TEST FAIL \n\n");
            am_hal_gpio_output_set(AM_BSP_GPIO_LED3);
            am_hal_gpio_output_clear(AM_BSP_GPIO_LED0);
        }

        uint32_t duration, start_time = g_tick_ms;
        nand_print_bad_blocks();
        duration = g_tick_ms - start_time;
        printf("Took %lu ms, %lu blocks/second \n",duration, 
                        (uint32_t)(NUM_BLOCKS/((float)duration * 0.001f)));
        
        am_util_delay_ms(2000);
    }

    // Go to Deep Sleep.
    am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
    
}
