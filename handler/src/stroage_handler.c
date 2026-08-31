#include "stroage_handler.h"
#include "stm32u0xx_hal.h"


#define ON_CHIP_FLASH_END_ADDR  0x0801FFFF
#define ON_CHIP_PAGE_SIZE       2048


static bool on_chip_flash_0_if_addr_valid(uint32_t addr);
static bool on_chip_flash_1_if_addr_valid(uint32_t addr);
static stroage_status_t on_chip_flash_erase(uint32_t addr, uint16_t len);
static stroage_status_t on_chip_flash_write(uint32_t addr, uint8_t *const data, uint16_t len);
static stroage_status_t on_chip_flash_read(uint32_t addr, uint8_t *const out, uint16_t len);




static storage_ops_t on_chip_flash_0_storage_ops = 
{
    .min_erase_size = ON_CHIP_PAGE_SIZE,/* minimal erase unit is page */
    .max_storage_capacity = ON_CHIP_PAGE_SIZE * 2,/* use final 2 pages */
    .save_addr = ON_CHIP_FLASH_END_ADDR - ON_CHIP_PAGE_SIZE * 2 + 1,/* use final 2 pages */
    .pf_if_addr_valid = on_chip_flash_0_if_addr_valid,
    .pf_stroage_erase = on_chip_flash_erase,
    .pf_stroage_write = on_chip_flash_write,
    .pf_stroage_read = on_chip_flash_read
};

static storage_ops_t on_chip_flash_1_storage_ops =
{
    .min_erase_size = ON_CHIP_PAGE_SIZE,/* minimal erase unit is page */
    .max_storage_capacity = ON_CHIP_PAGE_SIZE,/* use 1 page */
    .save_addr = ON_CHIP_FLASH_END_ADDR - ON_CHIP_PAGE_SIZE * 3 + 1,/* third-to-last page (page 61): FLASH_0 occupies the final 2 pages (62-63) */
    .pf_if_addr_valid = on_chip_flash_1_if_addr_valid,
    .pf_stroage_erase = on_chip_flash_erase,
    .pf_stroage_write = on_chip_flash_write,
    .pf_stroage_read = on_chip_flash_read
};

static bool on_chip_flash_0_if_addr_valid(uint32_t addr)
{
    return ((addr>=on_chip_flash_0_storage_ops.save_addr && addr<=ON_CHIP_FLASH_END_ADDR));
}

static bool on_chip_flash_1_if_addr_valid(uint32_t addr)
{
    return ((addr>=on_chip_flash_1_storage_ops.save_addr && addr<=ON_CHIP_FLASH_END_ADDR));
}

static stroage_status_t on_chip_flash_erase(uint32_t addr, uint16_t len)
{    
    /* Calculate start page number from address */
    uint32_t start_page = (addr - 0x08000000) / ON_CHIP_PAGE_SIZE;
    uint32_t num_pages = (len + ON_CHIP_PAGE_SIZE - 1) / ON_CHIP_PAGE_SIZE;
    
    FLASH_EraseInitTypeDef flash = 
    {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Page = start_page,
        .NbPages = num_pages
    };
    HAL_StatusTypeDef flash_status;
    // __disable_irq();/* FLASH_WaitForLastOperation inner timeout machanism need interrupt */
    flash_status = HAL_FLASH_Unlock();
    if(HAL_OK != flash_status)
    {        
        STR_HAN_DEBUG_ERR("flash unlock err!\r\n");
        return STROAGE_ERR; 
    }
    /* Clear OPTVERR bit set on virgin samples */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);
    uint32_t PageError;  
    flash_status = HAL_FLASHEx_Erase(&flash, &PageError);
    HAL_FLASH_Lock();  
    // __enable_irq();/* FLASH_WaitForLastOperation inner timeout machanism need interrupt */
    if(HAL_OK != flash_status)
    {        
        STR_HAN_DEBUG_ERR("flash erase err!\r\n");
        return STROAGE_ERR; 
    }
    return STORAGE_OK;
}

static stroage_status_t on_chip_flash_write(uint32_t addr, uint8_t *const data, uint16_t len)
{
    // __disable_irq();
    
    HAL_StatusTypeDef flash_status;
    flash_status = HAL_FLASH_Unlock();
    if (flash_status != HAL_OK) 
    {
        STR_HAN_DEBUG_ERR("flash unlock err!\r\n");
        return STROAGE_ERR;
    }
    /* Write data in 64-bit (8-byte) chunks */
    uint16_t write_cnt = (len + sizeof(uint64_t) - 1) / sizeof(uint64_t);
    for(uint16_t i = 0; i < write_cnt; i++)
    {
        uint32_t write_addr = addr + 8 * i;
        uint64_t write_data = 0xFFFFFFFFFFFFFFFFULL; /* Default to erased value */
        
        /* Copy remaining bytes to write_data */
        uint16_t bytes_to_write = (len > 8) ? 8 : len;
        for(uint16_t j = 0; j < bytes_to_write; j++)
        {
            ((uint8_t*)&write_data)[j] = data[8*i + j];
        }
        len -= bytes_to_write;
        
        flash_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, write_addr, write_data);
        if (flash_status != HAL_OK)
        {
            STR_HAN_DEBUG_ERR("flash program err, addr=0x%08X!\r\n", write_addr);
            HAL_FLASH_Lock();
            // __enable_irq();
            return STROAGE_ERR;
        }
    }
    HAL_FLASH_Lock();
    // __enable_irq();
    return STORAGE_OK;
}

static stroage_status_t on_chip_flash_read(uint32_t addr, uint8_t *const out, uint16_t len)
{
    memcpy(out, (uint8_t*)addr, len);
    return STORAGE_OK;
}


storage_ops_t* stroage_factory_bulid(stroage_type_t stroage_type)
{
    storage_ops_t *storage_ops = NULL;
    switch(stroage_type)
    {
        case ON_CHIP_FLASH_0:
            storage_ops = &on_chip_flash_0_storage_ops;
            break;
        case ON_CHIP_FLASH_1:
            storage_ops = &on_chip_flash_1_storage_ops;
            break;
        default:
            break;
    }
    return storage_ops;
} 
