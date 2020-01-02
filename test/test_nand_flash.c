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

#include "metal/unit.h"

//*****************************************************************************
// Main
//*****************************************************************************
int main(void)
{
    system_init();
    am_hal_gpio_output_set(AM_BSP_GPIO_LED0);


    printf("Hello World \n!");

    METAL_ASSERT(1);

    am_hal_gpio_output_clear(AM_BSP_GPIO_LED0);
    return METAL_REPORT();
}


void _exit(int code) {
    am_hal_gpio_output_set(AM_BSP_GPIO_LED1);
    while(1);
}