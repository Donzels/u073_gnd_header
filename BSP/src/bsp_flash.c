/**
 * @file bsp_flash.c
 * @brief BSP on-chip Flash driver implementation.
 *
 * Wraps HAL flash operations (unlock, erase, program, lock) behind a small
 * board-level API. The handler layer calls bsp_flash_*() — never HAL_FLASH_*().
 */

#include "bsp_flash.h"
#include "stm32u0xx_hal.h"

/* -------------------------------------------------------------------------- */
/*                        Erase                                                */
/* -------------------------------------------------------------------------- */

bool bsp_flash_erase(uint32_t addr, uint16_t len)
{
    if (len == 0U)
    {
        return false;
    }

    /* Calculate start page number from address (Flash base = 0x08000000). */
    uint32_t start_page = (addr - 0x08000000U) / BSP_FLASH_PAGE_SIZE;
    uint32_t num_pages  = (len + BSP_FLASH_PAGE_SIZE - 1U) / BSP_FLASH_PAGE_SIZE;

    FLASH_EraseInitTypeDef erase =
    {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Page      = start_page,
        .NbPages   = num_pages,
    };

    HAL_StatusTypeDef status;

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return false;
    }

    /* Clear OPTVERR bit set on virgin samples. */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);

    uint32_t page_error = 0U;
    status = HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();

    return (status == HAL_OK);
}

/* -------------------------------------------------------------------------- */
/*                        Write (doubleword / 64-bit)                          */
/* -------------------------------------------------------------------------- */

bool bsp_flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return false;
    }

    HAL_StatusTypeDef status;

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return false;
    }

    /* Write data in 64-bit (8-byte) chunks. */
    uint16_t write_cnt = (len + sizeof(uint64_t) - 1U) / sizeof(uint64_t);
    uint16_t remaining = len;

    for (uint16_t i = 0U; i < write_cnt; i++)
    {
        uint32_t write_addr = addr + 8U * i;
        uint64_t write_data = 0xFFFFFFFFFFFFFFFFULL; /* Default to erased value. */

        /* Copy remaining bytes to write_data. */
        uint16_t bytes_to_write = (remaining > 8U) ? 8U : remaining;
        for (uint16_t j = 0U; j < bytes_to_write; j++)
        {
            ((uint8_t *)&write_data)[j] = data[8U * i + j];
        }
        remaining -= bytes_to_write;

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, write_addr, write_data);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();
    return true;
}

/* -------------------------------------------------------------------------- */
/*                        Read (memory-mapped)                                 */
/* -------------------------------------------------------------------------- */

bool bsp_flash_read(uint32_t addr, uint8_t *out, uint16_t len)
{
    if ((out == NULL) || (len == 0U))
    {
        return false;
    }

    /* On-chip flash is memory-mapped at 0x08000000; a simple memcpy suffices. */
    const uint8_t *src = (const uint8_t *)addr;
    for (uint16_t i = 0U; i < len; i++)
    {
        out[i] = src[i];
    }
    return true;
}
