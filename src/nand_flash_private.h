// Private functions for nand_flash
// Only for use for testing etc, not public API!

#include <stdint.h>
#include <stdbool.h>

uint32_t _nand_cmd_write_enable(void);

uint32_t _nand_cmd_write_disable(void);

uint32_t _nand_get_writable(bool *writable);

uint32_t _nand_cmd_page_read(uint32_t page_addr);
uint32_t _nand_cmd_read_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len);

uint32_t _nand_get_busy(bool *busy);

uint32_t nand_wait_busy(uint32_t timeout_us, bool *program_fail, bool *erase_fail,
                               uint8_t *status);

uint32_t _nand_cmd_program_load_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len);
uint32_t _nand_cmd_program_load_random_x1(uint16_t column_addr, uint32_t *data, uint32_t data_len);
uint32_t _nand_cmd_program_execute(uint32_t page_addr);
uint32_t _nand_cmd_block_erase(uint32_t page_addr);