/**
 * @file bsp_flash.h
 * @brief BSP on-chip Flash driver wrapper.
 *
 * Wraps HAL_FLASH_Unlock / HAL_FLASHEx_Erase / HAL_FLASH_Program / HAL_FLASH_Lock
 * so the handler layer never calls HAL flash functions directly.
 */

#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flash geometry (STM32U073: 2 KB pages, up to 64 KB total) */
#define BSP_FLASH_PAGE_SIZE      2048U
#define BSP_FLASH_END_ADDR       0x0801FFFFU

/**
 * @brief Erase one or more flash pages.
 * @param addr  First byte address to erase (must be page-aligned).
 * @param len   Number of bytes to erase (rounded up to whole pages).
 * @return true on success, false on error.
 */
bool bsp_flash_erase(uint32_t addr, uint16_t len);

/**
 * @brief Write data to flash in 64-bit (doubleword) programs.
 * @param addr  Destination address (must be 8-byte aligned).
 * @param data  Source buffer.
 * @param len   Number of bytes to write.
 * @return true on success, false on error.
 */
bool bsp_flash_write(uint32_t addr, const uint8_t *data, uint16_t len);

/**
 * @brief Read data from flash (memcpy wrapper for symmetry).
 * @param addr  Source address.
 * @param out   Destination buffer.
 * @param len   Number of bytes to read.
 * @return true always.
 */
bool bsp_flash_read(uint32_t addr, uint8_t *out, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_FLASH_H__ */
