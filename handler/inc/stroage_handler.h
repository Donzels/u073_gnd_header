#ifndef __STROAGE_HANDLER_H__
#define __STROAGE_HANDLER_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "dbg_console.h"
#include "SEGGER_RTT.h" 
/* #include "SEGGER_RTT.h"   
#include "SEGGER_RTT.h"                                                    */
/* extern int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...); */
/* #define STR_HAN_DEBUG_OUT(fmt, ...)  SEGGER_RTT_printf(0, fmt, ##__VA_ARGS__) */
#define STR_HAN_DEBUG_OUT(fmt, ...)      dbg_console_printf(fmt, ##__VA_ARGS__)   /* Output log to debug console */
#define STR_HAN_DEBUG_ERR(fmt, ...)      SEGGER_RTT_printf(0, RTT_CTRL_TEXT_BRIGHT_RED fmt RTT_CTRL_RESET, ##__VA_ARGS__)
typedef enum
{
    STORAGE_OK = 0,
    STROAGE_ERR,
}stroage_status_t;

typedef enum
{
    ON_CHIP_FLASH_0 = 0,
    ON_CHIP_FLASH_1,
}stroage_type_t;

typedef struct
{
    const uint32_t min_erase_size;
    const uint32_t max_storage_capacity;  
    const uint32_t save_addr;    
    bool (*pf_if_addr_valid)(uint32_t addr);
    stroage_status_t (*pf_stroage_erase)(uint32_t addr, uint16_t len);
    stroage_status_t (*pf_stroage_write)(uint32_t addr, uint8_t *const data, uint16_t len);
    stroage_status_t (*pf_stroage_read)(uint32_t addr, uint8_t *const out, uint16_t len);
}storage_ops_t;

storage_ops_t* stroage_factory_bulid(stroage_type_t stroage_type);

#endif //__STROAGE_HANDLER_H__
