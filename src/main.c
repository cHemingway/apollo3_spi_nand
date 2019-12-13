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

// Test buffer to store pages in
uint8_t write_page[PAGE_SIZE];
uint8_t read_page[PAGE_SIZE];

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

    #if TEST_MARK_BLOCK
    {
        uint32_t ret_code = 0;
        bool is_bad;
        printf("Marking block test... ");
        ret_code = nand_check_bad_block(4, &is_bad);
        if (is_bad) {
            printf(" Already Marked! ");
        }
        ret_code = nand_mark_bad_block(4); // Mark block 4 as bad, should fail
        ret_code = nand_check_bad_block(4, &is_bad);
        if (is_bad) {
            printf(" Success \n");
        } else {
            printf(" Failed! Block not marked as bad, or read incorrectly! \n");
        }

    }
    #endif

    #if TEST_PROGRAM_BLOCK
    {
        uint32_t ret_code;
        const uint32_t block_addr = 4;   // Block 4 is in gauranteed non-bad section
        const uint32_t page_addr = block_addr*PAGES_PER_BLOCK;
        bool ecc_err;

        printf("Block erase and reprogram test \n");

        // Generate Data
        for (int i=0; i<PAGE_SIZE; i++) {
            write_page[i] = ((i&0xf) << 4) | (i&0xf); // generate 00, 11, 22, 33 etc
        }

        // Erase page and write value
        ret_code = nand_erase_block(block_addr);
        if (ret_code) {
            printf("Failed! Erase block returned %lu",ret_code);
        }
        ret_code = nand_prog_page(page_addr, write_page);
        if (ret_code) {
            printf("Failed! Prog page returned %lu",ret_code);
        }

        // Read page 0 in between to clear cache
        ret_code = nand_read_page(0, 0, read_page, PAGE_SIZE, &ecc_err);

        // Read back
        ret_code = nand_read_page(page_addr, 0, read_page, PAGE_SIZE, &ecc_err);
        if (ret_code) {
            printf("Failed! Prog page returned %lu",ret_code);
        }

        // Check read correctly
        if(0 != memcmp(read_page, write_page, PAGE_SIZE)) {
            printf("Failed! Read vs Written does not match.");
        }

    }
    printf("Success! \n");
    #endif

    while(1) {
        retcode = nand_test();
        if (retcode == AM_HAL_STATUS_SUCCESS) {
            printf("FLASH TEST PASS \n\n");
            am_hal_gpio_output_set(AM_BSP_GPIO_LED0);
            am_hal_gpio_output_clear(AM_BSP_GPIO_LED3);
        } else { // Error
            printf("FLASH TEST FAIL \n\n");
            am_hal_gpio_output_set(AM_BSP_GPIO_LED3);
            am_hal_gpio_output_clear(AM_BSP_GPIO_LED0);
        }

        nand_print_bad_blocks();
        
        am_util_delay_ms(2000);
    }

    // Go to Deep Sleep.
    am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
    
}
