/**
 * @file bsp_wapi.h
 * @brief BSP WAPI module control — power, wake pin, and LPUART1 IDLE line.
 *
 * Wraps the GPIO and UART operations for the M0804C WAPI module so APP
 * never calls HAL_GPIO_* or __HAL_UART_* directly.
 */

#ifndef __BSP_WAPI_H__
#define __BSP_WAPI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Power / wake pin control (active-low: RESET = power on) ---- */
void bsp_wapi_pwr_on(void);          /* WAPI_PWR  = RESET */
void bsp_wapi_pwr_off(void);         /* WAPI_PWR  = SET   */
void bsp_wapi_wake_high(void);       /* WAPI_WAKE = SET   */
void bsp_wapi_wake_low(void);        /* WAPI_WAKE = RESET */

/* ---- Low-power: set WAPI pins to analog, disable LPUART1 IDLE ---- */
void bsp_wapi_enter_low_power(void);

/* ---- Wakeup: restore WAPI pins, re-enable LPUART1 IDLE ---- */
void bsp_wapi_exit_low_power(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_WAPI_H__ */
