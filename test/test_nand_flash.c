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

//*****************************************************************************
// Main
//*****************************************************************************
int main(void)
{
    system_init();


    printf("Hello World \n!");
}