/**
 * @file voltage.h
 * @brief Voltage management — PVD or ADC+DMA voltage monitoring.
 *
 * A compile-time macro selects the detection method:
 *   VOLTAGE_USE_ADC = 0  → PVD (Programmable Voltage Detector). Compares VDD
 *                           against a fixed threshold, EXTI interrupt on crossing.
 *   VOLTAGE_USE_ADC = 1  → ADC1 + DMA. Continuously samples the internal VREFINT,
 *                           computes VDDA in mV, compares against a threshold.
 *
 * The PVD method uses the STM32U073's built-in detector with 7 fixed thresholds
 * (≈2.0…2.9 V). The ADC method reads the factory-calibrated VREFINT through ADC1
 * with DMA and derives VDDA from the calibration formula:
 *     VDDA (mV) = VREFINT_CAL_VREF * VREFINT_CAL / adc_avg
 */

#ifndef __VOLTAGE_H__
#define __VOLTAGE_H__

#include <stdint.h>
#include <stdbool.h>

/* ---- Detection-method selection -------------------------------------------------
 * 0 = PVD (default): hardware detector, EXTI interrupt, fixed thresholds.
 * 1 = ADC + DMA:    ADC1 samples VREFINT continuously, DMA + TC interrupt,
 *                   software compares VDDA (mV) against a threshold.
 * ------------------------------------------------------------------------------- */
#ifndef VOLTAGE_USE_ADC
#define VOLTAGE_USE_ADC   1
#endif

#define VOLTAGE_DEBUG_OUT(fmt, ...)  dbg_console_printf(fmt "\r\n", ##__VA_ARGS__)

#define UNDERVOLTAGE_SLEEP_MS        (5*1000)   /* ms to sleep after under-voltage detected */

/* Threshold (mV) used by the ADC method. The computed VDDA is compared directly
 * against this value (with hysteresis); the voltage_level_t enum is NOT used by
 * the ADC path. Override at compile time to change the trip point. */
#ifndef VOLTAGE_ADC_THRESHOLD_MV
#define VOLTAGE_ADC_THRESHOLD_MV   2800U//3100U
#endif

/**
 * @brief Voltage event dispatched to the application callback.
 *
 * The detector fires on both edges; inside the ISR we decide whether VDD is
 * currently below or above the threshold and map that to one of these events.
 */
typedef enum
{
    VOLTAGE_EVENT_OK = 0,   /* VDD rose back above the PVD threshold           */
    VOLTAGE_EVENT_LOW,      /* VDD fell below the PVD threshold (low battery)   */
} voltage_event_t;

/**
 * @brief PVD threshold levels.
 *
 * Maps to the STM32U073 fixed PVD thresholds (PWR_PVDLEVEL_0 … 6). The values
 * are nominal; see the datasheet for the exact thresholds and hysteresis.
 * For the ADC method these are converted to a threshold in mV.
 */
typedef enum
{
    VOLTAGE_LEVEL_2_0V = 0,   /* ≈ 2.0 V  (PWR_PVDLEVEL_0) */
    VOLTAGE_LEVEL_2_2V,       /* ≈ 2.2 V  (PWR_PVDLEVEL_1) */
    VOLTAGE_LEVEL_2_4V,       /* ≈ 2.4 V  (PWR_PVDLEVEL_2) */
    VOLTAGE_LEVEL_2_5V,       /* ≈ 2.5 V  (PWR_PVDLEVEL_3) */
    VOLTAGE_LEVEL_2_6V,       /* ≈ 2.6 V  (PWR_PVDLEVEL_4) */
    VOLTAGE_LEVEL_2_8V,       /* ≈ 2.8 V  (PWR_PVDLEVEL_5) */
    VOLTAGE_LEVEL_2_9V,       /* ≈ 2.9 V  (PWR_PVDLEVEL_6) */
} voltage_level_t;

/**
 * @brief Application voltage-event callback.
 *
 * Called from interrupt context (keep it short). @p event tells whether the
 * latest crossing recovered (OK) or dropped (LOW).
 */
typedef void (*voltage_event_cb_t)(voltage_event_t event);

void init_voltage_monitoring(void);

/**
 * @brief Query the current under-voltage status.
 *
 * PVD method: reads PWR_FLAG_PVDO (true == VDD below threshold).
 * ADC method: returns the cached state from the last DMA evaluation.
 *
 * @return true if VDD is below the threshold, false otherwise.
 */
bool voltage_is_low(void);

/* -------------------------------------------------------------------------- */
/*                    Low-power enter / exit hooks                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prepare the voltage manager for low-power sleep.
 *
 * PVD method: disables the PVD to minimise sleep current.
 * ADC method: stops ADC + DMA.
 */
void voltage_enter_low_power(void);

/**
 * @brief Restore the voltage manager after waking from low-power sleep.
 *
 * PVD method: re-enables the PVD and clears any stale flag.
 * ADC method: re-calibrates and restarts ADC + DMA.
 */
void voltage_exit_low_power(void);

#endif /* __VOLTAGE_H__ */
