/*
 * Prints an ONFI Parameter Page to stdout
 * References Open Nand Flash Interface Specification Rev 2.0, 27-Feb-2008
 * Section 5.6.1: Parameter Page Data Structure Definition (page 94)
 * 
 * 
 * Author: Chris Hemingway
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>

#pragma once

uint32_t onfi_print(const uint8_t parameter_page[256], unsigned int page_len, bool detailed);