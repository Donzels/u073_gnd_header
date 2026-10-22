/**
 * @file bsp_system.h
 * @brief BSP system-level helpers — tick, UID, watchdog, system power, Stop 2.
 *
 * Wraps HAL_GetTick, HAL_GetUID, the WATCH_DOG_OUT / SYS_POWER_SW pins, and
 * HAL_PWREx_EnterSTOP2Mode so APP never calls them directly.
 */

#ifndef __BSP_SYSTEM_H__
#define __BSP_SYSTEM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Time ---- */
uint32_t bsp_get_tick(void);

/* ---- Unique ID (96 bits → 6 bytes, lower 16 bits of each word) ---- */
void bsp_get_uid(uint8_t *uid_6bytes);

/* ---- External watchdog (WATCH_DOG_OUT, active-high pulse) ---- */
void bsp_wdog_toggle(void);
void bsp_wdog_set_high(void);
void bsp_wdog_set_low(void);
void bsp_wdog_feed(void);

/* ---- System power switch (SYS_POWER_SW, SET = power on) ---- */
void bsp_sys_pwr_set_high(void);
void bsp_sys_pwr_set_low(void);

/* ---- Low-power: set system/debug pins to analog ---- */
void bsp_sys_enter_low_power(void);

/* ---- Wakeup: restore system/debug pins ---- */
void bsp_sys_exit_low_power(void);

/* ---- Enter Stop 2 (WFI) with full preparation ----
 * Disables SysTick, disables the TIM7/LPTIM2 IRQ, clears pending interrupts,
 * enters Stop 2 (WFI), then on wakeup re-enables clocks, SysTick and IRQ. */
void bsp_enter_stop2(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SYSTEM_H__ */
