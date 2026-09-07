/**
 * @file bsp_adc.c
 * @brief BSP ADC implementation — calibration + DMA + ConvCplt callback.
 *
 * The HAL_ADC_ConvCpltCallback override lives here. APP registers its handler
 * via bsp_adc_register_conv_cplt_cb().
 */

#include "bsp_adc.h"
#include "main.h"

/* Defined in main.c (CubeMX-generated) */
extern ADC_HandleTypeDef hadc1;

/* ---- Application callback (NULL if none) ---- */
static bsp_adc_conv_cplt_cb_t s_adc_conv_cplt_cb = NULL;

/* -------------------------------------------------------------------------- */
/*                        Calibration                                          */
/* -------------------------------------------------------------------------- */

void bsp_adc_calibrate(void)
{
    HAL_ADCEx_Calibration_Start(&hadc1);
}

/* -------------------------------------------------------------------------- */
/*                        DMA conversion                                       */
/* -------------------------------------------------------------------------- */

void bsp_adc_start_dma(uint32_t *buf, uint32_t len)
{
    HAL_ADC_Start_DMA(&hadc1, buf, len);
}

void bsp_adc_stop_dma(void)
{
    HAL_ADC_Stop_DMA(&hadc1);
}

/* -------------------------------------------------------------------------- */
/*                        Callback registration                                */
/* -------------------------------------------------------------------------- */

void bsp_adc_register_conv_cplt_cb(bsp_adc_conv_cplt_cb_t cb)
{
    s_adc_conv_cplt_cb = cb;
}

/* -------------------------------------------------------------------------- */
/*                        HAL callback override                                */
/* -------------------------------------------------------------------------- */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1)
    {
        if (s_adc_conv_cplt_cb != NULL)
        {
            s_adc_conv_cplt_cb();
        }
    }
}
