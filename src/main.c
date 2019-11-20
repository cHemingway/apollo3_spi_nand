// Template project for Ambiq Apollo 3, including Segger RTT and SysTick example
// Flashes LED on Sparkfun Edge board

// For printf, will pick up _write() in SEGGER_RTT_Syscalls_GCC.c
#include <stdio.h>

#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "system_apollo3.h"

#include "SEGGER_RTT.h"

#include "mspi_nand_flash.h"

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

    printf("Hello World \n");                  // Outputs over Segger RTT



    while(1) {
        retcode = mspi_nand_flash_init(&pHandle);
        if (AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS != retcode)
        {
            printf("Failed to configure the MSPI and Flash Device correctly!\n");
        }

        retcode = mspi_nand_flash_id();
        if (AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS != retcode)
        {
            printf("Invalid Flash ID!\n");
        }

        am_hal_gpio_state_write(AM_BSP_GPIO_LED0, AM_HAL_GPIO_OUTPUT_TOGGLE);
        am_util_delay_ms(250);
    }

    // Go to Deep Sleep.
    am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
    
}
