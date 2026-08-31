/**
 * @file low_power.h
 * @brief Low-power manager — ThreadX tickless idle + Stop 2 integration.
 *
 * Wires the ThreadX tickless-idle framework (tx_low_power.c) to the STM32U073
 * RTC wakeup timer and Stop 2 mode. The four TX_LOW_POWER_* hooks in tx_user.h
 * are mapped to these functions.
 *
 * Sleep strategy
 * --------------
 *   - Scheduled sleep: RTC wakeup timer fires after the next ThreadX timeout.
 *   - Event wakeup:    EXTI pins (WKUP_IO, HALL_EXTI) and RTC all wake Stop 2.
 *   - Mode:            Stop 2 (lowest power with RAM + register retention).
 *
 * Timing
 * ------
 * The wakeup counter is clocked at RTCCLK/16 = ~2048 Hz, giving ~0.49 ms
 * resolution over a ~32 s window (16-bit counter). Longer timeouts are handled
 * by re-entering sleep for the remainder.
 *
 * Wakeup-source caveat
 * --------------------
 * LPUART1 is clocked from PCLK1, so it CANNOT wake the device from Stop 2.
 * To wake on UART traffic, re-clock LPUART1 from LSE in CubeMX (or use Stop 1).
 */

#ifndef __LOW_POWER_H__
#define __LOW_POWER_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialise the low-power manager.
 *
 * Deactivates the CubeMX default wakeup timer and enables the RTC wakeup
 * interrupt in the NVIC. Called once from App_ThreadX_Init().
 */
void low_power_init(void);

/* ---- Timed sleep (seconds) ---- */

/**
 * @brief Enter Stop 2 for a fixed duration in seconds.
 *
 * Blocks the calling thread for the requested time using the RTC wakeup timer
 * with 1 Hz (1 count = 1 s) resolution. Can also be woken early by any enabled
 * EXTI source (e.g. hall pin). Clamped to ~18 hours (16-bit counter max).
 *
 * @param seconds  Sleep duration in seconds.
 */
void low_power_sleep_seconds(uint32_t seconds);

/* ---- Delay then enter Stop 2 (thread + queue) ---- */

/**
 * @brief Request a "delay then sleep" in milliseconds.
 *
 * Posts a request to the low-power manager thread, which delays for the requested
 * time and then enters Stop 2 directly (no RTC wakeup timer armed). The device
 * sleeps until woken by an external interrupt (e.g. hall pin). Non-blocking:
 * returns immediately. Multiple callers can request sleep; requests are queued
 * and honoured sequentially.
 *
 * @param delay_ms        Delay in milliseconds before entering Stop 2.
 * @param cancel_existing If true, all queued/pending requests are discarded first
 *                        so the new request becomes the sole pending one. Use this
 *                        to atomically "cancel existing + schedule new" without the
 *                        race where a prior low_power_sleep_cancel() would also clear
 *                        the brand-new request.
 */
void low_power_schedule_sleep_ms(uint32_t delay_ms, bool cancel_existing);


/* ---- ThreadX tickless-idle hooks (mapped via TX_LOW_POWER_* macros) ---- */
/* Currently unused: tickless idle is disabled. Kept for reference. */

// void low_power_timer_setup(uint32_t next_expiration_ms);
// void low_power_enter(void);
// void low_power_exit(void);
// uint32_t low_power_timer_adjust(void);

#endif /* __LOW_POWER_H__ */
