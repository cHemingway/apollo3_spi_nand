// Functions for system initialisation
// To be called from main

// Ambiq includes
#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "system_apollo3.h"
// Segger RTT
#include "SEGGER_RTT.h"


// Override am_print_string so hard_fault_handler outputs over RTT
void am_print_string(char *pcStr) {
    SEGGER_RTT_WriteString(0, pcStr);
}


// Increment g_tick_ms every systick
volatile uint32_t g_tick_ms;  // Systick counter
void SysTick_Handler(void)
{
    g_tick_ms++;
}


void system_init(void) {
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
}