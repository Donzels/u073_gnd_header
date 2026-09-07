/**
 * @file bsp_adc.h
 * @brief BSP ADC control — calibration + DMA start/stop + ConvCplt callback.
 *
 * The HAL_ADC_ConvCpltCallback override lives inside bsp_adc.c. APP registers
 * its handler via bsp_adc_register_conv_cplt_cb() so APP never writes a
 * HAL callback directly.
 */

#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application callback invoked from the DMA transfer-complete ISR
 *        (via the BSP's HAL_ADC_ConvCpltCallback override).
 */
typedef void (*bsp_adc_conv_cplt_cb_t)(void);

/* ---- Calibration ---- */
void bsp_adc_calibrate(void);

/* ---- DMA conversion ---- */
void bsp_adc_start_dma(uint32_t *buf, uint32_t len);
void bsp_adc_stop_dma(void);

/* ---- Callback registration ---- */
void bsp_adc_register_conv_cplt_cb(bsp_adc_conv_cplt_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ADC_H__ */
