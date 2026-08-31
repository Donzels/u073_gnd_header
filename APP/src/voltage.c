/**
 * @file voltage.c
 * @brief Voltage management implementation.
 *
 * Two detection methods, selected at compile time by VOLTAGE_USE_ADC:
 *
 *   VOLTAGE_USE_ADC = 0  → PVD (Programmable Voltage Detector).
 *     Compares VDD against one of seven fixed thresholds (≈2.0…2.9 V) and raises
 *     an EXTI interrupt on a crossing. HAL_PWR_PVDCallback() reads PWR_FLAG_PVDO
 *     to decide the level and dispatches a voltage_event_t.
 *
 *   VOLTAGE_USE_ADC = 1  → ADC1 + DMA + TC interrupt.
 *     ADC1 continuously samples the internal VREFINT channel; DMA circular mode
 *     fills a buffer; on each DMA transfer-complete interrupt we average the
 *     buffer, derive VDDA from the ST factory calibration, and compare against a
 *     threshold (with hysteresis) to dispatch events.
 *
 *     VDDA (mV) = VREFINT_CAL_VREF * (*VREFINT_CAL_ADDR) / adc_avg
 */

#include "voltage.h"
#include "stm32u0xx_hal.h"
#include "dbg_console.h"
#include "led.h"
#include "low_power.h"
#include "osal.h"

#if VOLTAGE_USE_ADC
#include "stm32u0xx_ll_adc.h"   /* VREFINT_CAL_ADDR, VREFINT_CAL_VREF */
#endif

/* -------------------------------------------------------------------------- */
/*                        Common state                                        */
/* -------------------------------------------------------------------------- */

/** Application callback registered via voltage_init(). NULL if none. */
static volatile voltage_event_cb_t g_voltage_cb = NULL;

/* -------------------------------------------------------------------------- */
/*                 VOLTAGE_USE_ADC == 1 : ADC1 + DMA method                   */
/* -------------------------------------------------------------------------- */
#if VOLTAGE_USE_ADC

/** DMA buffer depth. Each entry is one 256x-oversampled VREFINT result. */
#define VOLTAGE_ADC_BUF_SIZE   1U

/** Hysteresis (mV) applied to the threshold to prevent chatter at the boundary. */
#define VOLTAGE_HYSTERESIS_MV  100U

/** Depth of the voltage queue (ISR producer → voltage_is_low() consumer). */
#define VOLTAGE_QUEUE_DEPTH    4U

/** How long voltage_is_low() blocks waiting for a sample (ms). */
#define VOLTAGE_RECV_TIMEOUT_MS  50U

/** DMA buffer, written by ADC1 on each conversion. */
static uint16_t g_adc_buf[VOLTAGE_ADC_BUF_SIZE];

/** Cached under-voltage state (true == VDD below threshold). */
static volatile bool g_voltage_low = false;

/** Queue handle: the DMA callback posts VDDA (mV) here; voltage_is_low()
 * receives from it. Created once in voltage_init(). */
static osal_queue_handle_t g_voltage_queue = NULL;

/* Declared in main.c (CubeMX-generated); not declared extern in any header. */
extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;


/* Average the DMA buffer and convert to VDDA (mV) via the VREFINT calibration.
 * Returns 0 if the data is invalid (should not happen once a conversion runs). */
static uint32_t voltage_compute_vdda_mv(void)
{
    uint32_t sum = 0U;

    for (uint32_t i = 0U; i < VOLTAGE_ADC_BUF_SIZE; i++)
    {
        sum += g_adc_buf[i];
    }

    uint32_t avg = sum / VOLTAGE_ADC_BUF_SIZE;
    if (avg == 0U)
    {
        return 0U;
    }

    /* VREFINT_CAL: factory reading at VDDA = 3.0 V (VREFINT_CAL_VREF mV). */
    uint32_t cal = (uint32_t)(*VREFINT_CAL_ADDR);
    if (cal == 0U)
    {
        return 0U;  /* calibration missing (should not ship this way) */
    }

    /* VDDA (mV) = VREFINT_CAL_VREF * cal / avg. Use 64-bit to avoid overflow. */
    uint64_t vdda = (uint64_t)VREFINT_CAL_VREF * (uint64_t)cal / (uint64_t)avg;
    return (uint32_t)vdda;
}

/* Evaluate one conversion result and post VDDA (mV) to the queue. Runs in the
 * DMA transfer-complete ISR context, so the send is no-wait. */
static void voltage_adc_evaluate(void)
{
    uint32_t vdda = voltage_compute_vdda_mv();
    if (vdda == 0U)
    {
        return;
    }

    /* ISR context: never block. Drop the sample if the queue is full. */
    osal_queue_send(g_voltage_queue, &vdda, 0U);
}

static void voltage_init(voltage_level_t level, voltage_event_cb_t cb)
{
    g_voltage_cb  = cb;
    g_voltage_low = false;
    (void)level;  /* ADC method uses VOLTAGE_ADC_THRESHOLD_MV, not the enum */

    /* Create the result queue once. */
    if (g_voltage_queue == NULL)
    {
        int32_t qret = osal_queue_create(VOLTAGE_QUEUE_DEPTH, sizeof(uint32_t),
                                         &g_voltage_queue);
        if (qret != OSAL_SUCCESS)
        {
            VOLTAGE_DEBUG_OUT("voltage: queue create failed: %d", (int)qret);
        }
    }

    /* Calibrate once at init. NOTE: HAL_ADC_Start_DMA() is NOT called here —
     * each voltage_is_low() triggers an on-demand conversion and waits for the
     * result, so there is no continuously-running DMA to stop on sleep. */
    HAL_ADCEx_Calibration_Start(&hadc1);

    VOLTAGE_DEBUG_OUT("voltage: ADC init (threshold=%u mV)", VOLTAGE_ADC_THRESHOLD_MV);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1)
    {
        voltage_adc_evaluate();
    }
}

/* Trigger one on-demand conversion, wait for the result via the queue, and judge
 * it against VOLTAGE_ADC_THRESHOLD_MV (with hysteresis). Returns true if VDD is
 * below the threshold.
 *
 * NOTE: this blocks on osal_queue_receive(), so it must be called from thread
 * context (which it is — see switch_handler_thread). */
bool voltage_is_low(void)
{
    /* Flush any stale samples so we measure fresh. */
    uint32_t flush = 0U;
    while (osal_queue_receive(g_voltage_queue, &flush, 0U) == OSAL_SUCCESS)
    {
        /* discard */
    }

    /* Start a conversion. HAL_ADC_Start_DMA resets the per-handle callback, so
     * reinstall ours immediately after. */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buf, VOLTAGE_ADC_BUF_SIZE);

    /* Wait for the DMA callback to post the result. */
    uint32_t vdda = 0U;
    int32_t ret = osal_queue_receive(g_voltage_queue, &vdda, VOLTAGE_RECV_TIMEOUT_MS);

    /* Always stop ADC + DMA so they sit idle between on-demand calls. Single-
     * conversion mode already halts the ADC; this also disarms the circular DMA
     * channel. Doing it unconditionally (even on timeout) guarantees a clean
     * state for the next call. */
    HAL_ADC_Stop_DMA(&hadc1);

    if (ret != OSAL_SUCCESS)
    {
        VOLTAGE_DEBUG_OUT("voltage: sample timeout");
        return g_voltage_low;  /* no fresh data — keep last known state */
    }

    if (vdda < VOLTAGE_ADC_THRESHOLD_MV)
    {
        if (!g_voltage_low)
        {
            g_voltage_low = true;
            if (g_voltage_cb != NULL)
            {
                g_voltage_cb(VOLTAGE_EVENT_LOW);
            }
        }
    }
    else if (vdda > VOLTAGE_ADC_THRESHOLD_MV + VOLTAGE_HYSTERESIS_MV)
    {
        if (g_voltage_low)
        {
            g_voltage_low = false;
            if (g_voltage_cb != NULL)
            {
                g_voltage_cb(VOLTAGE_EVENT_OK);
            }
        }
    }

    return g_voltage_low;
}

void voltage_enter_low_power(void)
{
    /* On-demand model: no conversion is running while idle, so there is nothing
     * to stop. Placeholder for symmetry with the PVD path. */
    HAL_ADC_Stop_DMA(&hadc1);
    // HAL_ADCEx_DisableVoltageRegulator(&hadc1);
    VOLTAGE_DEBUG_OUT("voltage: ADC idle (sleep)");
}

void voltage_exit_low_power(void)
{
    // HAL_ADC_Init(&hadc1);
    /* Re-calibrate after wakeup; the next voltage_is_low() will start a fresh
     * conversion. */
    HAL_ADCEx_Calibration_Start(&hadc1);
    VOLTAGE_DEBUG_OUT("voltage: ADC recalibrated (wakeup)");
}

/* -------------------------------------------------------------------------- */
/*                 VOLTAGE_USE_ADC == 0 : PVD method                          */
/* -------------------------------------------------------------------------- */
#else

#include "stm32u0xx_ll_pwr.h"   /* LL_PWR_IsActiveFlag_PVDO() */

/**
 * @brief Override of the weak HAL PVD callback.
 *
 * Called from HAL_PWREx_PVD_PVM_IRQHandler() whenever the PVD EXTI rising or
 * falling flag fires. We read PWR_FLAG_PVDO (the live PVD output) to decide the
 * current level and dispatch the matching event to the application callback.
 *
 * PWR_FLAG_PVDO: SET → VDD is below the threshold (low); RESET → at/above (OK).
 */
void HAL_PWR_PVDCallback(void)
{
    if (g_voltage_cb == NULL)
    {
        return;
    }

    /* PVDO: 1 = VDD below threshold, 0 = VDD at/above threshold. */
    if (LL_PWR_IsActiveFlag_PVDO() != 0U)
    {
        g_voltage_cb(VOLTAGE_EVENT_LOW);
    }
    else
    {
        g_voltage_cb(VOLTAGE_EVENT_OK);
    }
}

static void voltage_init(voltage_level_t level, voltage_event_cb_t cb)
{
    g_voltage_cb = cb;

    /* Map the public voltage_level_t to the HAL PWR_PVDLEVEL_x constant. */
    static const uint32_t level_table[] =
    {
        [VOLTAGE_LEVEL_2_0V] = PWR_PVDLEVEL_0,
        [VOLTAGE_LEVEL_2_2V] = PWR_PVDLEVEL_1,
        [VOLTAGE_LEVEL_2_4V] = PWR_PVDLEVEL_2,
        [VOLTAGE_LEVEL_2_5V] = PWR_PVDLEVEL_3,
        [VOLTAGE_LEVEL_2_6V] = PWR_PVDLEVEL_4,
        [VOLTAGE_LEVEL_2_8V] = PWR_PVDLEVEL_5,
        [VOLTAGE_LEVEL_2_9V] = PWR_PVDLEVEL_6,
    };
    uint32_t pvd_level = level_table[(uint32_t)level];

    /* Configure threshold + interrupt mode (both edges). */
    PWR_PVDTypeDef pvd_config =
    {
        .PVDLevel = pvd_level,
        .Mode     = PWR_PVD_MODE_IT_RISING_FALLING,
    };
    HAL_PWR_ConfigPVD(&pvd_config);

    /* Clear any stale EXTI flag so the first event reflects the real state. */
    __HAL_PWR_PVD_EXTI_CLEAR_FLAG();

    /* Enable the detector. */
    HAL_PWR_EnablePVD();

    VOLTAGE_DEBUG_OUT("voltage: PVD init (level=%u)", (unsigned)level);
}

bool voltage_is_low(void)
{
    return (LL_PWR_IsActiveFlag_PVDO() != 0U);
}

void voltage_enter_low_power(void)
{
    HAL_PWR_DisablePVD();
    VOLTAGE_DEBUG_OUT("voltage: PVD disabled (sleep)");
}

void voltage_exit_low_power(void)
{
    HAL_PWR_EnablePVD();
    __HAL_PWR_PVD_EXTI_CLEAR_FLAG();
    VOLTAGE_DEBUG_OUT("voltage: PVD enabled (wakeup)");
}

#endif /* VOLTAGE_USE_ADC */

/* -------------------------------------------------------------------------- */
/*                        Common public API                                   */
/* -------------------------------------------------------------------------- */

static void undervoltage_callback(voltage_event_t event)
{
    if (event == VOLTAGE_EVENT_LOW)
    {
        low_power_schedule_sleep_ms(UNDERVOLTAGE_SLEEP_MS, true);
        VOLTAGE_DEBUG_OUT("voltage: low detected");
        led_on_off(UNDER_VOLTAGE, LED_CMD_ON);       
    }
    else if (event == VOLTAGE_EVENT_OK)
    {
        VOLTAGE_DEBUG_OUT("voltage: recovered");
        led_on_off(UNDER_VOLTAGE, LED_CMD_OFF);
    }
}

void init_voltage_monitoring(void)
{
    voltage_init(VOLTAGE_LEVEL_2_9V, undervoltage_callback);
}
