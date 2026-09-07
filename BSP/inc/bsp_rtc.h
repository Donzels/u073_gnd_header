/**
 * @file bsp_rtc.h
 * @brief BSP RTC control — wakeup timer, Alarm A, time/date, flags.
 *
 * Wraps the HAL_RTC* and __HAL_RTC* macros. The HAL_RTCEx_WakeUpTimerEventCallback
 * and HAL_RTC_AlarmAEventCallback overrides live inside bsp_rtc.c; APP registers
 * its handlers via bsp_rtc_wakeup_register_cb() / bsp_rtc_alarm_register_cb().
 */

#ifndef __BSP_RTC_H__
#define __BSP_RTC_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Callbacks (invoked from the BSP's HAL RTC callback overrides) ---- */
typedef void (*bsp_rtc_wakeup_cb_t)(void);
typedef void (*bsp_rtc_alarm_cb_t)(void);

/* ---- Time / date (binary format, matches HAL RTC_TimeTypeDef / RTC_DateTypeDef) ---- */
typedef struct
{
    uint8_t Hours;
    uint8_t Minutes;
    uint8_t Seconds;
} bsp_rtc_time_t;

typedef struct
{
    uint8_t Year;    /* 0..99 */
    uint8_t Month;   /* 1..12 */
    uint8_t Date;    /* 1..31 */
} bsp_rtc_date_t;

/* ---- Wakeup timer (CK_SPRE, 1 count = 1 s) ---- */
void bsp_rtc_wakeup_set(uint32_t seconds);
void bsp_rtc_wakeup_stop(void);
uint32_t bsp_rtc_wakeup_get(void);

/* ---- Alarm A (one-shot calendar match) ---- */
void bsp_rtc_alarm_set(const bsp_rtc_time_t *time);
void bsp_rtc_alarm_stop(void);

/* ---- Time / date ---- */
void bsp_rtc_get_time(bsp_rtc_time_t *time, bsp_rtc_date_t *date);

/* ---- Sync / flags ---- */
void bsp_rtc_wait_synchro(void);
void bsp_rtc_wakeup_clear_flag(void);
bool bsp_rtc_wakeup_get_flag(void);
void bsp_rtc_alarm_clear_flag(void);

/* ---- Callback registration ---- */
void bsp_rtc_wakeup_register_cb(bsp_rtc_wakeup_cb_t cb);
void bsp_rtc_alarm_register_cb(bsp_rtc_alarm_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RTC_H__ */
