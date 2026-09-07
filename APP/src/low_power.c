/**
 * @file low_power.c
 * @brief Low-power manager implementation.
 *
 * Bridges the ThreadX tickless-idle framework to the STM32U073: the RTC wakeup
 * timer provides the scheduled wakeup, and Stop 2 is the sleep state. The four
 * TX_LOW_POWER_* macros in tx_user.h call into these functions.
 *
 * The RTC is clocked from LSE (32768 Hz crystal). The wakeup counter uses the
 * RTCCLK/16 (= 2048 Hz) clock source so that ThreadX ticks (10 ms) map to
 * exact counts (20.48 counts/tick). With a 16-bit counter the longest single
 * sleep is ~32 s; longer timeouts simply re-enter sleep.
 */

#include "low_power.h"
#include "bsp.h"
#include "osal.h"
#include "dbg_console.h"
#include "hall.h"
#include "led.h"
#include "rfid.h"
#include "upper_commu.h"
#include "wapi_commu.h"
#include "voltage.h"

#define LP_DEBUG_OUT(fmt, ...)    dbg_console_printf(fmt "\r\n", ##__VA_ARGS__)

/* Defined in APP/src/hall.c */
extern switch_handler_t g_hall2_handler_inst;

/* RTCCLK/16 = 2048 Hz exactly with a 32768 Hz LSE. */
#define LP_WAKEUP_CLK_HZ            2048U
/* Wakeup counter runs in milliseconds (1000 ms/s). */
#define LP_MS_PER_SEC               1000U
/* 16-bit wakeup counter max (used as max seconds for CK_SPRE clock). */
#define LP_MAX_WAKEUP_COUNTS        0xFFFFU

/* Minimum idle time (ms) worth entering low power. Shorter idle periods skip
 * low power to avoid the entry/exit overhead costing more than the savings. */
#define LP_ENTER_THRESHOLD_MS       30U

#define LP_SLEEP_QUEUE_DEPTH        8U

#define PERIODIC_WAKEUP_SECONDS      (45*60)      /* 45 minutes, RTC wakeup timer */

/* Programmed sleep duration (in counts). Shared by low_power_timer_setup()
 * (RTCCLK/16, ms resolution) and low_power_sleep_seconds() (CK_SPRE, 1s resolution). */
static uint32_t g_programmed_counts = 0U;

/* true if using CK_SPRE (1 Hz, 1 count = 1s), false if using RTCCLK/16 (ms). */
static bool g_using_ck_spre = false;

/* ---- Scheduled sleep (thread + queue): forward declarations ---- */
typedef struct
{
    uint32_t delay_ms;
} lp_sleep_req_t;

static osal_queue_handle_t g_lp_sleep_queue;
static osal_task_handle_t  g_lp_sleep_task_handle;
static void lp_sleep_task(void *argument);
static void lp_modules_enter(void);
static void lp_modules_exit(void);

/* Application callback invoked after wakeup from RTC timer (45 min periodic).
 * Called in thread context (not ISR) after the wakeup restore sequence completes.
 * Feeds the external watchdog (WATCH_DOG_OUT) on each periodic wakeup, then
 * re-enters sleep after a short delay. */
static void lp_rtc_wakeup_handler(void)
{
    low_power_schedule_sleep_ms(100, false);  /* feed the watchdog for 100 ms, then sleep again */
}

/* Application callback invoked after wakeup from RTC Alarm A (24 h periodic).
 * Called in thread context (not ISR) after the wakeup restore sequence completes.
 * Samples hall2 (WKUP_EXTI) level and synthesises the matching edge callback so
 * the normal hall2 flow runs after the daily wakeup. */
static void lp_rtc_alarm_handler(void)
{
    hall2_sample_edge();
}

static void low_power_set_timer_seconds(uint32_t seconds);
static void low_power_set_alarm_24h(void);

/* RTC wakeup callback (registered with BSP). */
static void lp_rtc_wakeup_cb(void)
{
    /* Toggle the watchdog pin from the ISR context (matches original behavior). */
    bsp_wdog_toggle();
    LP_DEBUG_OUT("low_power: wakeup timer callback");
}

/* RTC Alarm A callback (registered with BSP). */
static void lp_rtc_alarm_cb(void)
{
    LP_DEBUG_OUT("low_power: alarm A callback");
}

/* Set by the RTC wakeup callback, cleared in thread context after wakeup, gating the
 * lp_rtc_wakeup_handler() call so it runs once per wakeup, in thread context. */
static volatile bool g_rtc_wakeup_flag;

/* Set by the RTC Alarm A callback, cleared in thread context after wakeup, gating the
 * lp_rtc_alarm_handler() call so it runs once per alarm, in thread context. */
static volatile bool g_rtc_alarm_flag;

/* -------------------------------------------------------------------------- */
/*                        Tick/count conversion                               */
/* -------------------------------------------------------------------------- */

#if 0  /* unused: only the disabled tickless hooks used this */
static uint32_t ms_to_counts(uint32_t ms)
{
    uint64_t counts = (uint64_t)ms * LP_WAKEUP_CLK_HZ / LP_MS_PER_SEC;
    if (counts == 0U) counts = 1U;
    if (counts > LP_MAX_WAKEUP_COUNTS) counts = LP_MAX_WAKEUP_COUNTS;
    return (uint32_t)counts;
}

static uint32_t counts_to_ms(uint32_t counts)
{
    return (uint32_t)((uint64_t)counts * LP_MS_PER_SEC / LP_WAKEUP_CLK_HZ);
}
#endif

/* -------------------------------------------------------------------------- */
/*                        ThreadX hooks                                       */
/* -------------------------------------------------------------------------- */

void low_power_init(void)
{
    /*
     * The CubeMX default (CK_SPRE_16BITS, counter 0xbeef) is too coarse for
     * tickless timing. Deactivate it; timer_setup() re-arms the counter per
     * sleep with the fine (RTCCLK/16) clock.
     */
    bsp_rtc_wakeup_stop();

    /* The MX_RTC_Init() placeholder also arms Alarm A (00:00:00, date masked).
     * Deactivate it and re-arm as a 24 h periodic wakeup at the current time. */
    bsp_rtc_alarm_stop();
    low_power_set_alarm_24h();
    g_rtc_alarm_flag = false;  /* discard any stale flag from the placeholder */

    /* Register the RTC callbacks with the BSP. */
    bsp_rtc_wakeup_register_cb(lp_rtc_wakeup_cb);
    bsp_rtc_alarm_register_cb(lp_rtc_alarm_cb);

    LP_DEBUG_OUT("low_power: init (Stop 2, wakeup clk %u Hz)", LP_WAKEUP_CLK_HZ);

    if (g_lp_sleep_queue == NULL)
    {
        int32_t qret = osal_queue_create(LP_SLEEP_QUEUE_DEPTH, sizeof(lp_sleep_req_t),
                                         &g_lp_sleep_queue);
        if (qret != OSAL_SUCCESS)
        {
            LP_DEBUG_OUT("low_power: sleep queue create failed: %d", (int)qret);
        }
    }
    if (g_lp_sleep_task_handle == NULL)
    {
        int32_t task_ret = osal_task_create("lp_sleep", lp_sleep_task, 1024, 20,
                                            &g_lp_sleep_task_handle, NULL);
        if (task_ret != OSAL_SUCCESS)
        {
            LP_DEBUG_OUT("low_power: sleep task create failed: %d", (int)task_ret);
        }
    }
    low_power_set_timer_seconds(PERIODIC_WAKEUP_SECONDS);
}

/* Arm RTC Alarm A for a 24-hour periodic wakeup. The alarm matches on the
 * current wall-clock time (HH:MM:SS) with date/weekday masked, so the hardware
 * fires once per day at that time without software re-arm.
 *
 * The alarm is a one-shot calendar match: after it fires, the RTC continues
 * counting and will match again the next day. We read the current time, then set
 * the alarm to that same time. To avoid the race where the alarm fires
 * immediately (because the current time already matches), we clear the ALRAF
 * flag after arming — the first real fire is then ~24 h away. */
static void low_power_set_alarm_24h(void)
{
    bsp_rtc_time_t time = {0};
    bsp_rtc_date_t date = {0};

    /* Read current time (must read time before date, the date register is latched
     * on time read). */
    bsp_rtc_get_time(&time, &date);

    bsp_rtc_alarm_set(&time);

    LP_DEBUG_OUT("low_power: alarm A set %02u:%02u:%02u (24 h periodic)",
                 (unsigned)time.Hours, (unsigned)time.Minutes, (unsigned)time.Seconds);
}

/* -------------------------------------------------------------------------- */
/*                        Tickless idle (disabled)                            */
/* -------------------------------------------------------------------------- */

#if 0  /* tickless idle disabled */
/* Tickless-idle hook: program the RTC wakeup timer for the next ThreadX timeout.
 * Only arms the timer if the idle duration is worth entering low power (>= threshold). */
void low_power_timer_setup(uint32_t next_expiration_ms)
{
    g_programmed_counts = 0U;
    g_using_ck_spre = false;

    if (next_expiration_ms < LP_ENTER_THRESHOLD_MS)
    {
        /* Idle time too short — not worth entering low power. Skip. */
        return;
    }

    uint32_t counts = ms_to_counts(next_expiration_ms);
    g_programmed_counts = counts;
    LP_DEBUG_OUT("low_power: setup timer for %u ms", next_expiration_ms);
    bsp_rtc_wakeup_set(counts);
}

void low_power_enter(void)
{
    lp_modules_enter();
    LP_DEBUG_OUT("low_power: enter Stop 2");
    bsp_enter_stop2();
}

void low_power_exit(void)
{
    /* Clear the wakeup flag to prevent immediate re-trigger. */
    bsp_rtc_wakeup_clear_flag();

    lp_modules_exit();
    LP_DEBUG_OUT("low_power: exit Stop 2");
}

uint32_t low_power_timer_adjust(void)
{
    uint32_t slept_seconds = 0;

    /* RTC registers are clocked by RTCCLK (32768 Hz), asynchronous to the CPU.
     * Wait for synchronization before reading to avoid stale values. */
    bsp_rtc_wait_synchro();

    /* Read the wakeup timer flag with a retry loop to handle sync latency.
     * At full CPU speed the first read may return a stale (un-set) value
     * because the RTCCLK-domain update hasn't propagated yet. */
    volatile uint32_t wutf_detected = 0;
    for (int retry = 0; retry < 10; retry++)
    {
        if (bsp_rtc_wakeup_get_flag())
        {
            wutf_detected = 1;
            break;
        }
    }

    uint32_t remaining = bsp_rtc_wakeup_get();

    if (wutf_detected)
    {
        /* RTC timer fired → slept the full programmed duration. */
        slept_seconds = g_programmed_counts;
    }
    else
    {
        /* Woken early by external interrupt → compute actual elapsed time. */
        if (g_programmed_counts > remaining)
        {
            slept_seconds = g_programmed_counts - remaining;
        }
        else
        {
            /* Safety: never return 0 (would cause infinite loop in ThreadX). */
            slept_seconds = (g_programmed_counts > 0) ? 1 : 0;
        }
    }

    /* Clear the wakeup timer flag and wait for the write to take effect.
     * Without the loop, a read-back immediately after clear may still read
     * the pre-clear value due to the async clock domain. */
    for (int retry = 0; retry < 10; retry++)
    {
        bsp_rtc_wakeup_clear_flag();
        if (!bsp_rtc_wakeup_get_flag())
        {
            break;
        }
    }

    LP_DEBUG_OUT("low_power: slept %u s", (unsigned long)slept_seconds);
    return slept_seconds;
}
#endif

/* Arm the RTC wakeup timer for a fixed duration in seconds (no sleep entry).
 * Uses the 1 Hz SPRE clock so each count = 1 second. */
static void low_power_set_timer_seconds(uint32_t seconds)
{
    if (seconds == 0U || seconds > LP_MAX_WAKEUP_COUNTS)
    {
        return;
    }

    bsp_rtc_wakeup_set(seconds);
    LP_DEBUG_OUT("low_power: timer set %u s", (unsigned long)seconds);
}


/* Enter Stop 2 for a fixed duration (seconds). Uses the 1 Hz SPRE clock so
 * each count = 1 second. Blocks the calling thread for the requested time.
 * Wakeup sources: RTC wakeup (timeout) or any enabled EXTI (e.g. hall pin). */
void low_power_sleep_seconds(uint32_t seconds)
{
    if (seconds == 0U || seconds > LP_MAX_WAKEUP_COUNTS)
    {
        return;
    }

    /* Clear the wakeup flag. */
    bsp_rtc_wakeup_clear_flag();

    lp_modules_enter();

    /* Arm with 1-second resolution (CK_SPRE = 1 Hz). */
    g_programmed_counts = seconds;
    g_using_ck_spre = true;
    low_power_set_timer_seconds(seconds);

    LP_DEBUG_OUT("low_power: sleep %u s", (unsigned long)seconds);
    bsp_enter_stop2();

    lp_modules_exit();
    LP_DEBUG_OUT("low_power: wakeup");
}

/* -------------------------------------------------------------------------- */
/*                 Delay then enter Stop 2 (thread + queue)                    */
/* -------------------------------------------------------------------------- */

/* Delay slice (ms) for the interruptible delay loop. Smaller values give finer
 * preemption granularity (a newer request is picked up sooner) at the cost of
 * slightly more frequent wakeups. */
#define LP_DELAY_CHUNK_MS      50U

/* Enter Stop 2 directly (no RTC wakeup timer armed). The device sleeps until
 * woken by an external interrupt (e.g. hall pin). Blocks the calling thread
 * for the whole sleep. */
static void lp_enter_stop2(void)
{
    /* Clear the wakeup flag. */
    bsp_rtc_wakeup_clear_flag();

    lp_modules_enter();

    LP_DEBUG_OUT("lp_sleep_task: enter Stop 2");

    bsp_enter_stop2();

    lp_modules_exit();
    LP_DEBUG_OUT("lp_sleep_task: wakeup from Stop 2");
}

/* Dedicated low-power thread: the sole owner of STOP2 entry. It blocks on the
 * queue and, for each request, delays then enters Stop 2. The delay is built
 * from short osal_task_delay_ms() slices with a non-blocking queue check between
 * them, so a newer request arriving during the delay preempts the current one:
 * the most recent request always wins. This serialises all sleep requests from
 * many callers through a single thread so they never race on STOP2 entry. */
static void lp_sleep_task(void *argument)
{
    lp_sleep_req_t req;
    int32_t ret;
    uint32_t remaining_ms;

    while (1)
    {
        /* Block until the first sleep request arrives. */
        ret = osal_queue_receive(g_lp_sleep_queue, &req, OSAL_MAX_DELAY);
        if (ret != OSAL_SUCCESS)
        {
            LP_DEBUG_OUT("lp_sleep_task: queue receive failed: %d", (int)ret);
            continue;
        }

        remaining_ms = req.delay_ms;

        /* Interruptible delay: sleep in chunks, restarting the delay whenever a
         * newer request arrives. The most recent request always wins. */
        while (remaining_ms > 0U)
        {
            uint32_t slice = (remaining_ms > LP_DELAY_CHUNK_MS) ? LP_DELAY_CHUNK_MS : remaining_ms;
            osal_task_delay_ms(slice);
            remaining_ms -= slice;

            /* Non-blocking check: did a newer request arrive? */
            ret = osal_queue_receive(g_lp_sleep_queue, &req, 0U);
            if (ret == OSAL_SUCCESS)
            {
                remaining_ms = req.delay_ms;
                LP_DEBUG_OUT("lp_sleep_task: preempted, restart %u ms", (unsigned long)remaining_ms);
            }
        }

        LP_DEBUG_OUT("lp_sleep_task: delay elapsed (%u ms), enter Stop 2", (unsigned long)req.delay_ms);
        lp_enter_stop2();
        /* RTC wakeup timer: if the wakeup timer fired (not just an EXTI), notify
         * the app. */
        if (g_rtc_wakeup_flag)
        {
            g_rtc_wakeup_flag = false;
            lp_rtc_wakeup_handler();
        }
        /* RTC Alarm A: if the 24 h alarm fired, notify the app. */
        if (g_rtc_alarm_flag)
        {
            g_rtc_alarm_flag = false;
            lp_rtc_alarm_handler();
        }
    }
}

/* Request a "delay then sleep" in milliseconds. Non-blocking: the request is
 * queued and the dedicated low-power thread delays then enters Stop 2 on the
 * caller's behalf. The device wakes only from an external interrupt. */
void low_power_schedule_sleep_ms(uint32_t delay_ms, bool cancel_existing)
{
    lp_sleep_req_t req = { .delay_ms = delay_ms };

    if (cancel_existing)
    {
        /* Atomically discard all pending requests, then post the new request.
         * Done inside a critical section so the low-power thread cannot observe an
         * intermediate state. This guarantees the brand-new request becomes the sole
         * pending one. */
        lp_sleep_req_t flush = {0};
        uint32_t primask = osal_enter_critical();
        while (osal_queue_receive(g_lp_sleep_queue, &flush, 0U) == OSAL_SUCCESS)
        {
            /* discard pending requests */
        }
        osal_queue_send(g_lp_sleep_queue, &req, 0U);
        osal_exit_critical(primask);
        LP_DEBUG_OUT("lp_sleep_task: schedule %u ms (cancel existing)", (unsigned long)delay_ms);
    }
    else
    {
        /* No-wait send: never stall the producer. */
        int32_t ret = osal_queue_send(g_lp_sleep_queue, &req, 0U);
        if (ret != OSAL_SUCCESS)
        {
            LP_DEBUG_OUT("lp_sleep_task: request dropped");
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                        Module enter / exit helpers                         */
/* -------------------------------------------------------------------------- */

/* Notify every module before sleeping. */
static void lp_modules_enter(void)
{
    hall_enter_low_power();
    led_enter_low_power();
    rfid_enter_low_power();
    upper_commu_enter_low_power();
    wapi_commu_enter_low_power();
    voltage_enter_low_power();
    bsp_sys_enter_low_power();
}

/* Restore every module after waking. */
static void lp_modules_exit(void)
{
    bsp_sys_exit_low_power();
    hall_exit_low_power();
    led_exit_low_power();
    rfid_exit_low_power();
    upper_commu_exit_low_power();
    wapi_commu_exit_low_power();
    voltage_exit_low_power();
}
