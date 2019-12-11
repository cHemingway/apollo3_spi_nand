/*
 * Prints an ONFI Parameter Page to stdout
 * References Open Nand Flash Interface Specification Rev 2.0, 27-Feb-2008
 * Section 5.6.1: Parameter Page Data Structure Definition (page 94)
 * 
 * 
 * Author: Chris Hemingway
 * SPDX-License-Identifier: MIT
 */



#include <stdio.h>
#include <stdint.h>
#include <string.h> //memcmp
#include <stdbool.h>

const int signature_offset = 0;
const uint8_t signature[] = {0x4F, 0x4E, 0x46, 0x49}; // "ONFI"

const int revision_offset = 4;
const int revision_max = 2; // Max revision is ONFI 2.0. Allow Revision 0 (reserved)

/* Offsets of MFN/MPN strings. Note: Need to specify printf length as well! */
const int device_mfn_offset = 32;
const int device_mpn_offset = 44;

const int jedec_mfn_id_offset = 64;
const int date_code_offset = 65;

/* Memory Organisation Block */
const int bytes_per_page_offset         = 80;
const int spare_bytes_per_page_offset   = 84;
const int pages_per_block_offset        = 92;
const int blocks_per_lun_offset         = 96;
const int num_lun_offset                = 100;
const int num_addr_cycles_offset        = 101;
const int bits_per_cell_offset          = 102;
const int bad_blocks_max_per_lun_offset = 103;
const int block_endurance_offset        = 105;
const int gauranteed_valid_blocks_offset= 107;
const int gauranteed_blocks_endurance_offset= 108;
const int num_programs_per_page_offset  = 110;
const int num_bits_ecc_correctable_offset= 112;
// TODO: Bitfields

/* Electrical Parameters Block */



/* Portable extraction methods for little endian terms in ONFI page
 * see https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html 
 */
static uint16_t extract_int16(const uint8_t *b) {
    return (b[0] << 0) | (b[1] << 8);
}

static uint32_t extract_int32(const uint8_t *b) {
    return (b[0] << 0) | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

/* Functions to print a parameter given the name, minus _offset suffix
 * e.g. PRINT_PARAM_UINT16(bytes_per_page_offset) prints "  bytes_per_page: 12 \n"
 * Note 2 leading spaces are included for alignment
 */
#define PRINT_PARAM_UINT8(NAME)  printf("  " #NAME ": %u \n", parameter_page[NAME ## _offset])
#define PRINT_PARAM_UINT16(NAME) printf("  " #NAME ": %u \n", extract_int16(parameter_page + NAME ## _offset))
#define PRINT_PARAM_UINT32(NAME) printf("  " #NAME ": %lu \n", extract_int32(parameter_page+ NAME ## _offset))


/*
 * Pretty-Prints a parameter_page to stdout
 * If detailed, prints all blocks, otherwise just manufacturer information and total size
 * Returns 0 on success, -1 on failure, with more complex description to stderr
 */
uint32_t onfi_print(const uint8_t parameter_page[256], size_t page_len, bool detailed) {
    // Check page len
    if (page_len < 256) {
        fprintf(stderr, "page_len is not long enough! \n");
        return 1;
    }

    // Check signature
    if (memcmp(parameter_page+signature_offset, signature, sizeof(signature))) {
        fprintf(stderr, "Invalid signature \n");
        return 1;
    }

    // TODO: Check CRC?

    // Check ONFI Revision
    uint8_t revision = parameter_page[revision_offset];
    if (revision > revision_max) {
        fprintf(stderr, "Unknown revision \n");
        return 1;
    }

    // TODO: Feature Support
    // TODO: Command Support

    // ** Manufacturer information block
    printf("MANUFACTURER INFORMATION BLOCK: \n");
    {

        // Print manufacturer name and JEDEC ID
        // Here we specify the length of the string for printf, as it is not null terminated
        printf("  Manufacturer: %.12s  \n", parameter_page + device_mfn_offset);
        // Here we have to convert this to an integer to print as hex, as
        // unsigned char hex format (hhx) doesn't seem to be in newlib-nano?
        int jedec_id = parameter_page[jedec_mfn_id_offset];
        printf("  JEDEC ID:     0x%02X \n", jedec_id);
        printf("  Model:        %.20s \n", parameter_page + device_mpn_offset);
        printf("  Date Code:    0x%04X \n", 
               extract_int16(parameter_page + date_code_offset));
    
    }
    
    uint32_t bytes_per_page = extract_int32(parameter_page + bytes_per_page_offset);
    uint16_t pages_per_block = extract_int16(parameter_page + pages_per_block_offset);
    uint16_t blocks_per_lun = extract_int16(parameter_page + blocks_per_lun_offset);
    uint8_t logical_units = parameter_page[num_lun_offset];
    
    // ** Memory Organisation Block, todo: Print only if detailed
    if (detailed) {
        printf("MEMORY ORGANISATION BLOCK: \n");
        {
            PRINT_PARAM_UINT32(bytes_per_page);
            PRINT_PARAM_UINT16(spare_bytes_per_page);
            PRINT_PARAM_UINT32(pages_per_block);
            PRINT_PARAM_UINT32(blocks_per_lun);
            PRINT_PARAM_UINT8(num_lun);
            PRINT_PARAM_UINT8(num_addr_cycles);
            PRINT_PARAM_UINT8(bits_per_cell);
            PRINT_PARAM_UINT16(bad_blocks_max_per_lun);
            PRINT_PARAM_UINT16(block_endurance);
            PRINT_PARAM_UINT8(gauranteed_valid_blocks);
            PRINT_PARAM_UINT16(gauranteed_blocks_endurance);
            PRINT_PARAM_UINT8(num_programs_per_page);
            PRINT_PARAM_UINT8(num_bits_ecc_correctable);
        }
    }

    //  Show total size in Mega Bytes and how it is calculated

    uint32_t total_size = bytes_per_page * pages_per_block
                          * blocks_per_lun * logical_units;

    printf("CAPACITY: %lu MB", total_size / (1024*1024));
    printf(" = %lu bytes * %u pages * %u blocks * %u units \n",
            bytes_per_page, pages_per_block, blocks_per_lun, logical_units);
    return 0; // Success

}