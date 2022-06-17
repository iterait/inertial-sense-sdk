#ifndef D_FLASH_H_
#define D_FLASH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

typedef enum
{
	FLASH_IDLE = 0,
	FLASH_BUSY,
	FLASH_ERROR,
	FLASH_OPTIONBYTES
} eFlashStatus;

typedef enum
{
	FLASH_RET_OK = 0,
	FLASH_RET_ERROR,
	FLASH_RET_INVALID,
	FLASH_RET_BUSY,
} eFlashRetStatus;

typedef struct
{
	// General
	eFlashStatus	status;
	uint32_t		estimatedOpTimeMs;			// The estimated operation time in milliseconds, based on the above table max timings

	// Settings
	uint32_t 		*writeStartSource;			// The start of the data, at SINGLE word granularity, not including padding words if present
	uint32_t 		*writeStartDestination;		// The start of the destination, at SINGLE word granularity, not including padding words if present
	uint32_t 		writeBytesNum;				// The number of bytes of data to write. Does not include padding bytes.
	bool 			noPageErase;				// Do not erase before a write

	// Erase state
	uint32_t 		eraseCurrentPage;
	uint32_t 		erasePagesRemaining;

	// Program state
	uint32_t		*writeCurrentSource;		// The current start of the data, at DOUBLE word granularity, including padding words if present
	uint32_t		*writeCurrentDestination;	// The current start of the destination, at DOUBLE word granularity, including padding words if present
	uint32_t 		writeWordsRemaining;		// The number of words pending, with DOUBLE word granularity, including padding words if present
	uint64_t 		writeFirstDWord;			// Holds first WORD if it is not aligned to start of first DWORD. 0xFFFF'FFFF'FFFF'FFFFF is reset
	uint64_t 		writeLastDWord;				// Holds last WORD if it is not aligned to second half of last DWORD. 0xFFFF'FFFF'FFFF'FFFFF is reset
} s_flash;

#define FLASH_PAGE_SIZE                		((uint32_t)0x200)		// 512B
#define FLASH_BLOCK_SIZE 					((uint32_t)0x2000)		// 8K
#define FLASH_NUM_OF_PAGES_PER_BLOCK 		(FLASH_BLOCK_SIZE / FLASH_PAGE_SIZE)

#define FLASH_START_ADDRESS             	((uint32_t)0x00400000)

#define FLASH_USER_DATA_SIZE 				((uint32_t)0x8000) 		// 4 blocks, 1 reserved
#define FLASH_USER_DATA_START_ADDRESS 		((uint32_t)0x000FA000)
#define FLASH_USER_DATA_END_ADDRESS 		((uint32_t)0x000FFFFF)
#define FLASH_USER_DATA_START_ABSOLUTE		(FLASH_START_ADDRESS+FLASH_USER_DATA_START_ADDRESS)

#define FLASH_CALIB_BASE_ADDRESS			FLASH_USER_DATA_START_ADDRESS
#define FLASH_CONFIG_BASE_ADDRESS			(FLASH_CALIB_BASE_ADDRESS + FLASH_BLOCK_SIZE)
#define FLASH_CONFIG_MIRROR_BASE_ADDRESS	(FLASH_CONFIG_BASE_ADDRESS + FLASH_BLOCK_SIZE)

#define FLASH_TRIES							10U						// Try up to this many times to write to flash before giving up,
#define FLASH_TIME_MAX						300U					// for this many milliseconds each


void EFC_Handler(void);

void flash_init_v2(void);
uint32_t flash_write_v2(uint32_t address, const void* newData, int dataSize, int noPageErase);
uint32_t flash_update_block(uint32_t address, const void *newData, int dataSize, int noPageErase);
uint32_t flash_update_block_from_rtos(uint32_t address, const void *newData, int dataSize, int noPageErase);
void flash_erase_chip(void);
void flash_erase_program(void);
int flash_write_in_progress(void);
uint32_t flash_get_user_signature(volatile void* ptr, uint32_t size);
uint32_t flash_set_user_signature(const volatile void *ptr, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif // D_FLASH_H_