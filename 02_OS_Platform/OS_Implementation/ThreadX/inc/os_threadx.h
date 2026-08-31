#ifndef __OS_THREADX_H__
#define __OS_THREADX_H__

#include <string.h>
#include "tx_api.h"

#define OS_MS_TO_TICKS(osal_time_in_ms) \
    ((osal_time_in_ms == OSAL_MAX_DELAY)? (TX_WAIT_FOREVER): ((osal_tick_type_t)(((osal_tick_type_t)(osal_time_in_ms) * (osal_tick_type_t)TX_TIMER_TICKS_PER_SECOND) / (osal_tick_type_t)1000U)))

#define OSAL_HEAP_POOL_SIZE         20480     

#endif // __OS_THREADX_H__
