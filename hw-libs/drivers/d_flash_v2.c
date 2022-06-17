#include <asf.h>

#include "d_flash_v2.h"
#include "d_watchdog.h"
#include "d_time.h"
#ifndef BOOTLOADER
#include "../SDK/hw-libs/misc/rtos.h"
#include "globals.h"
#endif

#ifdef DEBUG
#pragma GCC push_options
#pragma GCC optimize ("O0")
#endif

#define EFC_PASSWORD (0x5A << 24)

/*
 * See Datasheet pg. 2112 for flash memory characteristics, including programming and erase sequence times
 *
 * The max values in this table are "guaranteed by design"
 *
 * 															Typ.		Max.		Unit
 * From datasheet:
 * 		one page (512 byte) programming time (normal):		1.5		         		ms
 * 		one page (512 byte) erase time:						10.0		50.0		ms
 *      one block (8k byte) erase time:						80.0		200.0		ms
 * Calculated:
 * 		Theoretical user pages programming
 * 			including erase and write (32K, normal):		416.0 		896.0+		ms
 *
 * We can use the FLASH EOP "end of operation" interrupt bit to achieve quick programming without blocking
 */

#define PAGE_ERASE_TIME_MAX_MS				(50U)
#define BLOCK_ERASE_TIME_MAX_MS				(200U)
#define PAGE_WRITE_TIME_TYP_MS_DIV	        (1.5f)	// Divide by this to get milliseconds a DWORD takes to program

s_flash flashMain = {0};

static uint32_t flash_update_block_interrupt(void);

static uint32_t flash_check_errors(void);
static void flash_quit_programming(void);

static uint32_t flash_erase_page_interrupt(void);
static uint32_t flash_program_word_normal_interrupt(void);

static uint32_t flash_unlock_v2(void);
static uint32_t flash_lock_v2(void);

static void flash_disable_cache(void);
static void flash_flush_cache_and_enable(void);

int flash_maintenance(void);

static uint32_t g_frdy = 1;

/*
 * FLASH peripheral interrupt handler. Called when a FLASH erase or write operation finishes, or there is an error.
 */
int flash_maintenance(void)
{
	// Look for rising edges
	if(g_frdy == 1 || !(EFC->EEFC_FSR & EEFC_FSR_FRDY))
	{
		return;
	}
	
	g_frdy = 1;

	uint32_t status = flash_check_errors();

	// Check whether we finished an operation
	if((status == FLASH_RET_OK) && (flashMain.status == FLASH_BUSY))
	{
		uint32_t opstatus;

		opstatus = flash_erase_page_interrupt();
		if(opstatus == FLASH_RET_BUSY)
		{
			return 0;		// We successfully started a programming sequence
		}

		opstatus = flash_program_word_normal_interrupt();
		if(opstatus == FLASH_RET_BUSY)
		{
			return 0;		// We successfully started a programming sequence
		}

		// verify flash write
		if (memcmp((const void*)flashMain.writeStartDestination, (const void*)flashMain.writeStartSource, flashMain.writeBytesNum) == 0)
		{
			flashMain.status = FLASH_IDLE;

			flash_lock_v2();
			flash_flush_cache_and_enable();

			return 1;
		}
	}
	else if(flashMain.status != FLASH_IDLE)
	{
		flashMain.status = FLASH_ERROR;
		flash_flush_cache_and_enable();
		
		return -1;
	}
	
	return 0;
}

void flash_init_v2(void)
{
	flash_unlock_v2();

// 	NVIC_DisableIRQ(EFC_IRQn);
// 
// 	EFC->EEFC_FMR |= EEFC_FMR_FRDY;					// Enable interrupt in peripheral
// 
// 	NVIC_ClearPendingIRQ(EFC_IRQn);
// 	NVIC_SetPriority(EFC_IRQn, 1);
// 	NVIC_EnableIRQ(EFC_IRQn);

	flash_check_errors();							// Clear existing error bits, set genFaultCode if needed
	flashMain.status = FLASH_IDLE;

	flash_lock_v2();
}

uint32_t flash_update_block(uint32_t address, const void *newData, int dataSize, int noPageErase)
{
	if(flashMain.status == FLASH_BUSY) return FLASH_RET_ERROR;

	flashMain.status = FLASH_IDLE;

	flashMain.writeStartSource = (uint32_t*)newData;
	flashMain.writeStartDestination = (uint32_t*)address;
	flashMain.writeBytesNum = dataSize;
	flashMain.noPageErase = noPageErase != 0;

	flash_update_block_interrupt();
	
	// Delay until we think the flash operation will be done.
	volatile uint32_t start = time_ticks();
	volatile uint32_t ticktarget = (flashMain.estimatedOpTimeMs + 50) * TIME_TICKS_PER_MS;
	while (time_ticks() - start < ticktarget) if(flash_maintenance() != 0) break;

	int trycounter = FLASH_TRIES;

	while(flashMain.status != FLASH_IDLE)
	{
		watchdog_maintenance_force();

		// Delay until we think the flash operation will be done.
		uint32_t start = time_ticks();
		uint32_t ticktarget = (flashMain.estimatedOpTimeMs + 50) * TIME_TICKS_PER_MS;
		while (time_ticks() - start < ticktarget) if(flash_maintenance() != 0) break;

		if(flashMain.status == FLASH_ERROR || trycounter <= 0)
		{
			flash_quit_programming();
			return FLASH_RET_ERROR;
		}

		// Retry the write
		flash_update_block_interrupt();
		trycounter--;
	}

	return FLASH_RET_OK;
}

#ifndef BOOTLOADER
uint32_t flash_update_block_from_rtos(uint32_t address, const void *newData, int dataSize, int noPageErase)
{
	if(flashMain.status == FLASH_BUSY) return FLASH_RET_ERROR;

	flashMain.status = FLASH_IDLE;

	flashMain.writeStartSource = (uint32_t*)newData;
	flashMain.writeStartDestination = (uint32_t*)address;
	flashMain.writeBytesNum = dataSize;
	flashMain.noPageErase = noPageErase != 0;

	flash_update_block_interrupt();

	// Delay until we think the flash operation will be done. Write/erase time is "guaranteed by design"
	for(size_t i=0; i < flashMain.estimatedOpTimeMs + 50; i++)
	{
		vTaskDelay(1);		// Yield for other tasks
		if(flash_maintenance() != 0) break;
	}

	int trycounter = FLASH_TRIES;

	while(flashMain.status != FLASH_IDLE)
	{
		// Delay until we think the flash operation will be done. Write/erase time is "guaranteed by design"
		for(size_t i=0; i < flashMain.estimatedOpTimeMs + 50; i++)
		{
			vTaskDelay(1);		// Yield for other tasks
			if(flash_maintenance() != 0) break;
		}

		if(flashMain.status == FLASH_ERROR || trycounter <= 0)
		{
			flash_quit_programming();
			return FLASH_RET_ERROR;
		}

		// Retry the write
		flash_update_block_interrupt();
		trycounter--;
	}

	return FLASH_RET_OK;
}
#endif

/**
 * Update any size area of flash, not limited to block like flash_update_block. Can erase down to page size.
 *
 * For now, this calls update block. We have disabled some checks to allow smaller/larger writes
 */
uint32_t flash_write_v2(uint32_t address, const void* newData, int dataSize, int noPageErase)
{
	return flash_update_block(address, newData, dataSize, noPageErase);
}

/*
 * Fill out the following fields before calling:
 * 		flashMain.writeStartSource
 *		flashMain.writeStartDestination
 *		flashMain.writeBytesNum
 *		flashMain.noPageErase
 *
 *	the data pointed to by writeStartSource MUST NOT go out of scope until flashMain.status is not FLASH_BUSY
 */
static uint32_t flash_update_block_interrupt(void)
{
	if(flashMain.status == FLASH_ERROR)
	{
		return FLASH_RET_ERROR;
	}
	if(flashMain.status == FLASH_BUSY)
	{
		return FLASH_RET_ERROR;
	}


	if(((uint32_t)flashMain.writeStartDestination < FLASH_USER_DATA_START_ADDRESS) || (((uint32_t)flashMain.writeStartDestination + flashMain.writeBytesNum) > FLASH_USER_DATA_END_ADDRESS))
	{
//		return FLASH_RET_INVALID;	// Guard against writing outside user pages // Disabled to allow programming of other areas
	}
	if(flashMain.writeBytesNum > (int)FLASH_BLOCK_SIZE)
	{
//		return FLASH_RET_INVALID;	// Guard against writing larger than block // Disabled to allow programming of main flash in bootloader code
	}

	// Check for identical memory
	if (memcmp((const void*)flashMain.writeStartDestination, flashMain.writeStartSource, flashMain.writeBytesNum) == 0)
	{
		flashMain.status = FLASH_IDLE;
		return FLASH_RET_OK;
	}

	if(flashMain.noPageErase)
	{
		flashMain.erasePagesRemaining = 0;
	}
	else
	{
		flashMain.erasePagesRemaining = flashMain.writeBytesNum / FLASH_PAGE_SIZE;
		if(flashMain.writeBytesNum % FLASH_PAGE_SIZE) flashMain.erasePagesRemaining++;	// Add 1 if the data stretches into the next page
		flashMain.eraseCurrentPage = (0x3FFFFF & (uint32_t)flashMain.writeStartDestination) / FLASH_PAGE_SIZE;
		
		if(flashMain.eraseCurrentPage % 16 != 0)
		{
			return FLASH_RET_INVALID;	// 16 pages must be erased at once, aligned on 16 page boundaries
		}
	}

	flashMain.writeCurrentDestination = flashMain.writeStartDestination;
	flashMain.writeCurrentSource = flashMain.writeStartSource;
	flashMain.writeWordsRemaining = flashMain.writeBytesNum / sizeof(uint32_t);

	// Calculate how much time the operation will take, so we can delay until then
	flashMain.estimatedOpTimeMs = (uint32_t)((float)flashMain.erasePagesRemaining * ((float)PAGE_ERASE_TIME_MAX_MS + PAGE_WRITE_TIME_TYP_MS_DIV));

	// We are going to erase, then write to the flash block repeatedly until success or FLASH_TRIES times

	flashMain.status = FLASH_BUSY;
	
	g_nvr_manage_config.flash_write_count++;

	flash_unlock_v2();
	
	flash_disable_cache();

	if(!(EFC->EEFC_FSR & EEFC_FSR_FRDY))				// If the flash says it is still busy, stop programming.
	{
		flash_quit_programming();
		return FLASH_RET_ERROR;
	}

	// Clear error bits and set uINS error bits if needed. Each subsequent
	// flash operation calls this in the interrupt.
	flash_check_errors();

	if(flashMain.erasePagesRemaining == 0)
	{
		flash_program_word_normal_interrupt();	// Kick off the first write
	}
	else
	{
		flash_erase_page_interrupt();	// Kick off the first erase
	}

	return FLASH_RET_BUSY;
}

#ifdef ENABLE_WDT
#define UPDATE_WATCHDOG() { WDT->WDT_CR = 0xA5000000 | WDT_CR_WDRSTT; }
#else
#define UPDATE_WATCHDOG() {}
#endif

/**
 * Erases user program and user data. Does not erase bootloader.
 */
__no_inline RAMFUNC void flash_erase_chip(void)
{
	LED_COLOR_RED();

	cpu_irq_disable();
	UPDATE_WATCHDOG();
	
	// Make sure all blocks are unlocked
	flash_unlock_v2();
	
	// Issue erase - After this we cannot access any functions in flash as it will be gone.
	EFC->EEFC_FCR = EEFC_FCR_FKEY_PASSWD | EEFC_FCR_FCMD(EFC_FCMD_EA);
	while ((EFC->EEFC_FSR & EEFC_FSR_FRDY) != EEFC_FSR_FRDY)
	UPDATE_WATCHDOG();

	//Clear GPNVM Bits
	EFC->EEFC_FCR = EEFC_FCR_FKEY_PASSWD | EEFC_FCR_FARG(0) | EEFC_FCR_FCMD(EFC_FCMD_CGPB);		//Protect bit
	while ((EFC->EEFC_FSR & EEFC_FSR_FRDY) != EEFC_FSR_FRDY)
	UPDATE_WATCHDOG();
	
	EFC->EEFC_FCR = EEFC_FCR_FKEY_PASSWD | EEFC_FCR_FARG(1) | EEFC_FCR_FCMD(EFC_FCMD_CGPB);		//Enter SAM-BA
	while ((EFC->EEFC_FSR & EEFC_FSR_FRDY) != EEFC_FSR_FRDY)
	UPDATE_WATCHDOG();
	
	EFC->EEFC_FCR = EEFC_FCR_FKEY_PASSWD | EEFC_FCR_FARG(7) | EEFC_FCR_FCMD(EFC_FCMD_CGPB);		//TCM config
	while ((EFC->EEFC_FSR & EEFC_FSR_FRDY) != EEFC_FSR_FRDY)
	UPDATE_WATCHDOG();
	
	EFC->EEFC_FCR = EEFC_FCR_FKEY_PASSWD | EEFC_FCR_FARG(8) | EEFC_FCR_FCMD(EFC_FCMD_CGPB);		//TCM config
	while ((EFC->EEFC_FSR & EEFC_FSR_FRDY) != EEFC_FSR_FRDY)
	UPDATE_WATCHDOG();
	
	LEDS_ALL_OFF();

	//Reset Device
	RSTC->RSTC_CR = RSTC_CR_KEY_PASSWD | RSTC_CR_PROCRST;
	while(1);
}

int flash_write_in_progress(void)
{
	return (flashMain.status != FLASH_IDLE);
}

static uint32_t flash_unlock_v2(void)
{
	for(int i = 1984; i < 2048; i++)
	{
		EFC->EEFC_FCR = EEFC_FCR_FCMD(EFC_FCMD_CLB) | i << 8 | EFC_PASSWORD;
		while(!(EFC->EEFC_FSR & EEFC_FSR_FRDY)) {}
	}

	return FLASH_RET_OK;
}

static uint32_t flash_lock_v2(void)
{
	for(int i = 1984; i < 2048; i++)
	{
		EFC->EEFC_FCR = EEFC_FCR_FCMD(EFC_FCMD_SLB) | i << 8 | EFC_PASSWORD;
		while(!(EFC->EEFC_FSR & EEFC_FSR_FRDY)) {}
	}
	
	return FLASH_RET_OK;
}

static void flash_disable_cache(void)
{
//	SCB_DisableDCache();
//	SCB_DisableICache();		// Might not need because we are only writing to data areas
}

static void flash_flush_cache_and_enable(void)
{
//	SCB_EnableDCache();
	SCB_CleanInvalidateDCache();
//	SCB_InvalidateICache();
}

static uint32_t flash_program_word_normal_interrupt(void)
{
	if(flashMain.writeWordsRemaining <= 0)	// Clean exit
	{
		return FLASH_RET_OK;
	}
	
	if((uint32_t)flashMain.writeCurrentDestination % 512U && !flashMain.noPageErase)
	{
		return FLASH_RET_ERROR;
	}
	
	uint16_t writePage = (uint32_t)flashMain.writeCurrentDestination / 512;
	
	flash_disable_cache();

	flashMain.status = FLASH_BUSY;

	// Fill the latch buffer
	for(uint32_t i = 0; i < (512U / sizeof(uint32_t)); ++i) 
	{
		*(flashMain.writeCurrentDestination++) = *(flashMain.writeCurrentSource++);
		if(--flashMain.writeWordsRemaining <= 0) break;
	}
	
	// Execute the write
	EFC->EEFC_FCR = EEFC_FCR_FCMD(EFC_FCMD_WP) | writePage << 8 | EFC_PASSWORD;
	
	g_frdy = 0;

	return FLASH_RET_BUSY;
}

static uint32_t flash_erase_page_interrupt(void)
{
	if(flashMain.erasePagesRemaining <= 0)	// Clean exit
	{
		return FLASH_RET_OK;
	}
	
	if(flashMain.eraseCurrentPage % 16)
	{
		return FLASH_RET_ERROR;
	}

	flashMain.status = FLASH_BUSY;
	
	flash_disable_cache();

	EFC->EEFC_FCR = EEFC_FCR_FCMD(EFC_FCMD_EPA) | (0x2 << 8) | ((flashMain.eraseCurrentPage / 16) << 12) | EFC_PASSWORD;
	
	g_frdy = 0;

	flashMain.erasePagesRemaining -= 16U;
	flashMain.eraseCurrentPage += 16U;

	return FLASH_RET_BUSY;
}

static uint32_t flash_check_errors(void)
{
	uint32_t status = FLASH_RET_OK;
	uint32_t errflags =			
			EEFC_FSR_FLERR
			| EEFC_FSR_FLOCKE
			| EEFC_FSR_FCMDE;

	if(EFC->EEFC_FSR & errflags)
	{
		if(flashMain.status != FLASH_IDLE) 	// Only throw errors if we were doing something
		{
#ifndef BOOTLOADER
			g_sysParams.genFaultCode |= GFC_FLASH_WRITE_FAILURE;
#endif
			flashMain.status = FLASH_ERROR;
			status = FLASH_RET_ERROR;
		}
	}

	return status;
}

static void flash_quit_programming(void)
{
	flashMain.writeWordsRemaining = 0;
	flashMain.erasePagesRemaining = 0;
	flashMain.status = FLASH_ERROR;
#ifndef BOOTLOADER
	g_sysParams.genFaultCode |= GFC_FLASH_WRITE_FAILURE;
#endif
	flash_lock_v2();
	flash_flush_cache_and_enable();
}

uint32_t flash_get_user_signature(volatile void* ptr, uint32_t size)
{
	return flash_read_user_signature((uint32_t*)ptr, size / sizeof(uint32_t));
}

uint32_t flash_set_user_signature(const volatile void *ptr, uint32_t size)
{
	// get copy of user signature in critical section
	BEGIN_CRITICAL_SECTION
	
	uint32_t* ptrCopy = (uint32_t*)pvPortMalloc(size);
	memcpy(ptrCopy, (const void*)ptr, size);
	
	END_CRITICAL_SECTION

	// erase user signature first
	flash_erase_user_signature();
	
	// perform flash write with copy of data outside critical section, size for this function is
	//  number of 32 bit words.
	uint32_t result = flash_write_user_signature(ptrCopy, size / sizeof(uint32_t));
	vPortFree(ptrCopy);
	return result;
}

#ifdef DEBUG
#pragma GCC pop_options
#endif