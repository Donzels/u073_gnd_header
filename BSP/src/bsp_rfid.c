/**
 * @file bsp_rfid.c
 * @brief BSP RFID implementation — GPIO + TIM PWM / input-capture.
 */

#include "bsp_rfid.h"
#include "main.h"

/* Defined in main.c (CubeMX-generated) */
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim15;

/* -------------------------------------------------------------------------- */
/*                        Power / sleep / carrier control                     */
/* -------------------------------------------------------------------------- */

void bsp_rfid_pwr_on(void)
{
    HAL_GPIO_WritePin(GPIOA, RFID_PWR_Pin, GPIO_PIN_SET);
}

void bsp_rfid_pwr_off(void)
{
    HAL_GPIO_WritePin(GPIOA, RFID_PWR_Pin, GPIO_PIN_RESET);
}

void bsp_rfid_sleep_on(void)
{
    HAL_GPIO_WritePin(GPIOA, RFID_SLEEP_Pin, GPIO_PIN_SET);
}

void bsp_rfid_sleep_off(void)
{
    HAL_GPIO_WritePin(GPIOA, RFID_SLEEP_Pin, GPIO_PIN_RESET);
}

void bsp_rfid_pwm_n_high(void)
{
    HAL_GPIO_WritePin(GPIOA, PWM_N_Pin, GPIO_PIN_SET);
}

void bsp_rfid_pwm_n_low(void)
{
    HAL_GPIO_WritePin(GPIOA, PWM_N_Pin, GPIO_PIN_RESET);
}

/* -------------------------------------------------------------------------- */
/*                        125 kHz carrier PWM (TIM2_CH3)                      */
/* -------------------------------------------------------------------------- */

void bsp_rfid_carrier_start(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
}

void bsp_rfid_carrier_stop(void)
{
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
}

/* -------------------------------------------------------------------------- */
/*                        Input capture (TIM15_CH1/CH2)                        */
/* -------------------------------------------------------------------------- */

void bsp_rfid_ic_start(void)
{
    HAL_TIM_IC_Start_IT(&htim15, TIM_CHANNEL_1);
    HAL_TIM_IC_Start_IT(&htim15, TIM_CHANNEL_2);
}

void bsp_rfid_ic_stop(void)
{
    HAL_TIM_IC_Stop_IT(&htim15, TIM_CHANNEL_1);
    HAL_TIM_IC_Stop_IT(&htim15, TIM_CHANNEL_2);
}

bsp_rfid_edge_t bsp_rfid_get_capture_edge(void)
{
    /* CC1 = rising (DIRECTTI), CC2 = falling (INDIRECTTI).
     * Matches handler's capture_edge_t ordering: RISING=0, FALLING, ERR. */
    if (__HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC1))
    {
        return BSP_RFID_EDGE_RISING;
    }
    if (__HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC2))
    {
        return BSP_RFID_EDGE_FALLING;
    }
    return BSP_RFID_EDGE_ERR;
}

uint32_t bsp_rfid_get_ic_value(bsp_rfid_edge_t edge)
{
    uint32_t ch = (edge == BSP_RFID_EDGE_RISING) ? TIM_CHANNEL_1 : TIM_CHANNEL_2;
    return HAL_TIM_ReadCapturedValue(&htim15, ch);
}

uint32_t bsp_rfid_get_arr(void)
{
    return __HAL_TIM_GET_AUTORELOAD(&htim15);
}

/* -------------------------------------------------------------------------- */
/*                        Low-power enter / exit                              */
/* -------------------------------------------------------------------------- */

void bsp_rfid_enter_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIOA: PA2 (TIM2_CH3) / PA3 (PWM_N) / PA4 (RFID_PWR) / PA5 (RFID_SLEEP) */
    GPIO_InitStruct.Pin  = GPIO_PIN_2 | PWM_N_Pin | RFID_PWR_Pin | RFID_SLEEP_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* GPIOB: PB14 (TIM15_CH1) */
    GPIO_InitStruct.Pin = GPIO_PIN_14;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void bsp_rfid_exit_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA2: TIM2_CH3, AF1 */
    GPIO_InitStruct.Pin       = GPIO_PIN_2;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA3/PA4/PA5: PWM_N / RFID_PWR / RFID_SLEEP, push-pull output */
    GPIO_InitStruct.Pin       = PWM_N_Pin | RFID_PWR_Pin | RFID_SLEEP_Pin;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0U;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB14: TIM15_CH1, AF14 */
    GPIO_InitStruct.Pin       = GPIO_PIN_14;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF14_TIM15;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
