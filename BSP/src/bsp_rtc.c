/**
 * @file bsp_rtc.c
 * @brief BSP RTC implementation — wakeup timer, Alarm A, time/date, flags.
 *
 * The HAL_RTCEx_WakeUpTimerEventCallback and HAL_RTC_AlarmAEventCallback
 * overrides live here. APP registers its handlers via
 * bsp_rtc_wakeup_register_cb() / bsp_rtc_alarm_register_cb().
 */

#include "bsp_rtc.h"
#include "main.h"

/* Defined in main.c (CubeMX-generated) */
extern RTC_HandleTypeDef hrtc;

/* ---- Application callbacks (NULL if none) ---- */
static bsp_rtc_wakeup_cb_t s_rtc_wakeup_cb = NULL;
static bsp_rtc_alarm_cb_t  s_rtc_alarm_cb  = NULL;

/* -------------------------------------------------------------------------- */
/*                        Wakeup timer (CK_SPRE, 1 count = 1 s)               */
/* -------------------------------------------------------------------------- */

/* Auto-select clock source by range:
 *   seconds <= 65535  → RTC_WAKEUPCLOCK_CK_SPRE_16BITS, pass seconds directly
 *   seconds >  65535  → RTC_WAKEUPCLOCK_CK_SPRE_17BITS, pass (seconds - 65536) */
#define BSP_RTC_16BIT_MAX_S   0xFFFFU
#define BSP_RTC_17BIT_MAX_S   0x1FFFFU

void bsp_rtc_wakeup_set(uint32_t seconds)
{
    if (seconds == 0U || seconds > BSP_RTC_17BIT_MAX_S)
    {
        return;
    }

    uint32_t clock;
    uint32_t reg_value;

    if (seconds <= BSP_RTC_16BIT_MAX_S)
    {
        clock     = RTC_WAKEUPCLOCK_CK_SPRE_16BITS;
        reg_value = seconds;
    }
    else
    {
        clock     = RTC_WAKEUPCLOCK_CK_SPRE_17BITS;
        reg_value = seconds - BSP_RTC_16BIT_MAX_S - 1U;
    }

    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, reg_value, clock, 0U);
}

void bsp_rtc_wakeup_stop(void)
{
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
}

uint32_t bsp_rtc_wakeup_get(void)
{
    return HAL_RTCEx_GetWakeUpTimer(&hrtc);
}

/* -------------------------------------------------------------------------- */
/*                        Alarm A (one-shot calendar match)                   */
/* -------------------------------------------------------------------------- */

void bsp_rtc_alarm_set(const bsp_rtc_time_t *time)
{
    RTC_AlarmTypeDef sAlarm = {0};
    sAlarm.AlarmTime.Hours             = time->Hours;
    sAlarm.AlarmTime.Minutes           = time->Minutes;
    sAlarm.AlarmTime.Seconds           = time->Seconds;
    sAlarm.AlarmTime.SubSeconds        = 0U;
    sAlarm.AlarmMask                   = RTC_ALARMMASK_DATEWEEKDAY;
    sAlarm.AlarmSubSecondMask          = RTC_ALARMSUBSECONDMASK_ALL;
    sAlarm.AlarmDateWeekDaySel         = RTC_ALARMDATEWEEKDAYSEL_DATE;
    sAlarm.AlarmDateWeekDay            = 1U;
    sAlarm.Alarm                       = RTC_ALARM_A;

    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
    HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);
    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRAF);
}

void bsp_rtc_alarm_stop(void)
{
    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
}

/* -------------------------------------------------------------------------- */
/*                        Time / date                                         */
/* -------------------------------------------------------------------------- */

void bsp_rtc_get_time(bsp_rtc_time_t *time, bsp_rtc_date_t *date)
{
    RTC_TimeTypeDef hal_time = {0};
    RTC_DateTypeDef hal_date = {0};

    HAL_RTC_GetTime(&hrtc, &hal_time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &hal_date, RTC_FORMAT_BIN);

    time->Hours   = hal_time.Hours;
    time->Minutes = hal_time.Minutes;
    time->Seconds = hal_time.Seconds;

    date->Year  = hal_date.Year;
    date->Month = hal_date.Month;
    date->Date  = hal_date.Date;
}

/* -------------------------------------------------------------------------- */
/*                        Sync / flags                                         */
/* -------------------------------------------------------------------------- */

void bsp_rtc_wait_synchro(void)
{
    HAL_RTC_WaitForSynchro(&hrtc);
}

void bsp_rtc_wakeup_clear_flag(void)
{
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
}

bool bsp_rtc_wakeup_get_flag(void)
{
    return (__HAL_RTC_WAKEUPTIMER_GET_FLAG(&hrtc, RTC_FLAG_WUTF) != RESET);
}

void bsp_rtc_alarm_clear_flag(void)
{
    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRAF);
}

/* -------------------------------------------------------------------------- */
/*                        Callback registration                                */
/* -------------------------------------------------------------------------- */

void bsp_rtc_wakeup_register_cb(bsp_rtc_wakeup_cb_t cb)
{
    s_rtc_wakeup_cb = cb;
}

void bsp_rtc_alarm_register_cb(bsp_rtc_alarm_cb_t cb)
{
    s_rtc_alarm_cb = cb;
}

/* -------------------------------------------------------------------------- */
/*                        HAL callback overrides                               */
/* -------------------------------------------------------------------------- */

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc)
{
    (void)hrtc;
    if (s_rtc_wakeup_cb != NULL)
    {
        s_rtc_wakeup_cb();
    }
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    (void)hrtc;
    if (s_rtc_alarm_cb != NULL)
    {
        s_rtc_alarm_cb();
    }
}
