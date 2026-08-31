#ifndef __OSAL_CONFIG_H__
#define __OSAL_CONFIG_H__

#include "common_types.h"

#define FREERTOS_SUPPORT    (1)
#define ZEPHYR_SUPPORT      (2)
#define THREADX_SUPPORT     (3) 

/* Which rtos to choose. */
#define OSAL_RTOS_SUPPORT (THREADX_SUPPORT)


#endif // __OSAL_CONFIG_H__
