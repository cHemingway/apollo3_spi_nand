// Command and register definitions for Micron MT79F 60 series flash
// Taken from m79a_2gb_1_8v_nand_spi.pdf datasheet

#define CMD_RESET 0xFF

#define CMD_WRITE_ENABLE        0x06
#define CMD_WRITE_DISABLE       0x04

#define CMD_PAGE_READ           0x13
#define CMD_READ_CACHE_SINGLE   0x03

#define CMD_READ_ID             0x9f
#define CMD_READ_CACHE_X4       0x6B
#define CMD_READ_CACHE_QUADIO   0xEB

#define CMD_PROGRAM_LOAD        0x02
#define CMD_PROGRAM_EXECUTE     0x10
#define CMD_PROGRAM_LOAD_RANDOM 0x84
#define CMD_PROGRAM_LOAD_RANDOM_QUAD 0x34

#define CMD_GET_FEATURES        0x0F
#define CMD_SET_FEATURES        0x1F

#define FEATURE_REG_BLOCK_LOCK 0xA0                     // Block lock feature register
#define FEATURE_REG_BLOCK_LOCK_UNLOCK_ALL    0x00        // Value to unlock all

#define FEATURE_REG_CONFIG     0xB0                      // Config feature register
#define FEATURE_REG_CONFIG_CFG_MASK                 0xC1 // Config register CFG bits mask
#define FEATURE_REG_CONFIG_CFG_VALUE_READ_PARAMS    0x40 // Config:CFG value for read parameter page


#define FEATURE_REG_STATUS     0xC0         // Status register address
#define FEATURE_REG_STATUS_CBRSY_MASK   0x80    // Mask for Cache Read Busy (CRBSY)
// TODO: ECC Status bits 0:2
#define FEATURE_REG_STATUS_P_FAIL_MASK  0x08    // Mask for program failure
#define FEATURE_REG_STATUS_E_FAIL_MASK  0x04    // Mask for erase failure
#define FEATURE_REG_STATUS_WEL_MASK     0x02    // Mask for Write Enable Latch (WEL) bit. 1=Writable
#define FEATURE_REG_STATUS_OIP_MASK     0x01    // Mask for Operation In Progress (OIP) bit. 1=Busy
#define FEATURE_REG_STATUS_HAS_ERRORS_MASK  (FEATURE_REG_STATUS_P_FAIL_MASK | FEATURE_REG_STATUS_E_FAIL_MASK)


#define FEATURE_REG_DIE_SELECT 0xD0    // Die select register

// #define nand_ID       0x462c
#define nand_ID       0x252c  // Byte reversed, LSB is first byte
#define nand_ID_MASK  0xfeff  // Allow both 0x2c46 (3.3V) and 0x2c47 (1.8V)

#define RESET_TIME_MS 1 // Takes 565uS to reset, round up to 1ms
#define ERASE_TIME_MS       10  // Max time to erase a block
#define PROGRAM_TIME_MS     1   // Max time to program a page, 600uS

#define PAGE_SIZE   (128+(2*1024))  // Page is 128 Metadata/ECC + 2K Data

#define PAGES_PER_BLOCK 64
#define LOG2_PPB        6
#define NUM_BLOCKS      2048

#define PARAMETER_PAGE_SIZE 256        // Parameter page is 255 bytes, repeats up to PAGE_SIZE
#define PARAMETER_PAGE_PAGE_ADDR 0x01  // Parameter table is page 0x01 (only in OTP/Param access mode)
#define PARAMETER_PAGE_COLUMN_ADDR 0x00 // Parameter table starts at column 0x00

#define BAD_BLOCK_PAGE_OFFSET 0x00      // Page within block that bad block marking is in
#define BAD_BLOCK_FACTORY_BYTE_OFFSET 2048      // Byte address of factory bad block marker
#define BAD_BLOCK_OUR_BYTE_OFFSET 2049          // Byte address of our bad block marker
#define BAD_BLOCK_MARKER_VALUE  0x00    // 0x00 in byte 2048 (1st spare) = bad block     