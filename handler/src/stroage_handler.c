#include "stroage_handler.h"
#include "bsp_flash.h"


#define ON_CHIP_FLASH_END_ADDR  BSP_FLASH_END_ADDR
#define ON_CHIP_PAGE_SIZE       BSP_FLASH_PAGE_SIZE


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
    if (!bsp_flash_erase(addr, len))
    {
        STR_HAN_DEBUG_ERR("flash erase err!\r\n");
        return STROAGE_ERR;
    }
    return STORAGE_OK;
}

static stroage_status_t on_chip_flash_write(uint32_t addr, uint8_t *const data, uint16_t len)
{
    if (!bsp_flash_write(addr, data, len))
    {
        STR_HAN_DEBUG_ERR("flash program err, addr=0x%08X!\r\n", addr);
        return STROAGE_ERR;
    }
    return STORAGE_OK;
}

static stroage_status_t on_chip_flash_read(uint32_t addr, uint8_t *const out, uint16_t len)
{
    if (!bsp_flash_read(addr, out, len))
    {
        return STROAGE_ERR;
    }
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
