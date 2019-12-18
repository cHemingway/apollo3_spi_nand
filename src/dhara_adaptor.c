/*
 * Adaptor for dhara between our nand_flash driver and dhara's nand.h
 * For function definitions, see dhara's nand.h
 * Chris Hemingway 2019
 */

#include "dhara/nand.h"
#include "dhara/error.h"
#include "nand_flash.h"

struct dhara_nand dhara_adaptor_nand = {
    .log2_page_size = LOG2_PAGE_SIZE,
    .log2_ppb = LOG2_PPB,
    .num_blocks = NUM_BLOCKS
};


int dhara_nand_is_bad(const struct dhara_nand *n, dhara_block_t b) {
    uint32_t retcode;
    bool is_bad;

    retcode = nand_check_bad_block(b, &is_bad);

    if (retcode != AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS) {
        // TODO: Do something different here?
        return DHARA_E_BAD_BLOCK;
    } else if (is_bad) {
        return DHARA_E_BAD_BLOCK;
    } else {
        return DHARA_E_NONE;
    }
}


void dhara_nand_mark_bad(const struct dhara_nand *n, dhara_block_t b) {
    // TODO: bad block marking does not work, so skipping for now?
    #warning Bad Block marking not implemented!
}


int dhara_nand_erase(const struct dhara_nand *n, dhara_block_t b,
		             dhara_error_t *err) {
    uint32_t retcode;
    retcode = nand_erase_block(b);
    if (retcode != AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS) {
        // "If an erase operation fails, return -1 and set err to E_BAD_BLOCK"
        *err = DHARA_E_BAD_BLOCK;
        return -1;
    } else {
        return 0;
    }
}


int dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
		    const uint8_t *data,
		    dhara_error_t *err) {
    uint32_t retcode;
    retcode = nand_prog_page(p,data);
    if (retcode != AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS) {
        // "If an erase operation fails, return -1 and set err to E_BAD_BLOCK"
        *err = DHARA_E_BAD_BLOCK;
        return -1;
    } else {
        return 0;
    }
}


int dhara_nand_is_free(const struct dhara_nand *n, dhara_page_t p) {
    bool is_free;
    uint32_t retcode = nand_is_free(p, &is_free);
    if (retcode != AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS) {
        // TODO: This is a fatal error, should reset or similar here
        return false;
    } else {
        return is_free ? 0 : 1;
    }
}


int dhara_nand_read(const struct dhara_nand *n, dhara_page_t p,
		    size_t offset, size_t length,
		    uint8_t *data,
		    dhara_error_t *err) {

    bool ecc_err;
    uint32_t retcode = nand_read_page(p, offset, data, length, &ecc_err);

    if (retcode != AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS) {
        return -1;
    } else if (ecc_err) {
        *err = DHARA_E_ECC;
        return -1;
    } else {
        return 0;
    }
}


int dhara_nand_copy(const struct dhara_nand *n,
		    dhara_page_t src, dhara_page_t dst,
		    dhara_error_t *err) {

    bool ecc_err;
    uint32_t retcode = nand_copy_page(src, dst, &ecc_err);
    if (retcode != AM_DEVICES_MSPI_FLASH_STATUS_SUCCESS) {
        return -1;
    } else if (ecc_err) {
        *err = DHARA_E_ECC;
        return -1;
    } else {
        return 0;
    }
}
