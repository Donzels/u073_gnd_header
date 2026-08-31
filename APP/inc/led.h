#ifndef __LED_H
#define __LED_H

/* Includes ------------------------------------------------------------------*/
#include "led_handler.h"

#include "dbg_console.h"
/* #include "SEGGER_RTT.h"                                                       */
/* extern int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...); */
/* #define LED_DEBUG_OUT(fmt, ...)  SEGGER_RTT_printf(0, fmt "\r\n", ##__VA_ARGS__)*/
#define LED_DEBUG_OUT(fmt, ...)      dbg_console_printf(fmt "\r\n", ##__VA_ARGS__)   /* Output log to debug console */

typedef enum
{
    UNDER_VOLTAGE = 0,                /* LED state: Under-voltage detected */
    NETWORK_ENTRY_FAIL,         /* LED state: Network entry failed */
    WAPI_COMMU,
    WAPI_COMMU_CONNING,
    WAPI_CONFIG,                /* LED state: WAPI configuration */   
    WAPI_CONFIG_CONNING,        
} led_lable_t;


void led_init(void);

void led_on_off(uint8_t led_lable, led_cmd_t led_cmd);
void led_off_all(void);

/* -------------------------------------------------------------------------- */
/*                    Low-power enter / exit hooks                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prepare the LED module for low-power sleep.
 *
 * Turns off all LEDs to minimise sleep current.
 */
void led_enter_low_power(void);

/**
 * @brief Restore the LED module after waking from low-power sleep.
 *
 * No-op on the U0: LED state is re-applied by the application as needed.
 * Present for symmetry / future use.
 */
void led_exit_low_power(void);

#endif /* __LED_H */