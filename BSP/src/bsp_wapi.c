/**
 * @file bsp_wapi.c
 * @brief BSP WAPI module implementation — power, wake pin, LPUART1 IDLE.
 */

#include "bsp_wapi.h"
#include "main.h"

/* Defined in main.c (CubeMX-generated) */
extern UART_HandleTypeDef hlpuart1;

/* Active-low power / wake: RESET = on, SET = off. */

void bsp_wapi_pwr_on(void)
{
    HAL_GPIO_WritePin(GPIOA, WAPI_PWR_Pin, GPIO_PIN_RESET);
}

void bsp_wapi_pwr_off(void)
{
    HAL_GPIO_WritePin(GPIOA, WAPI_PWR_Pin, GPIO_PIN_SET);
}

void bsp_wapi_wake_high(void)
{
    HAL_GPIO_WritePin(GPIOB, WAPI_WAKE_Pin, GPIO_PIN_SET);
}

void bsp_wapi_wake_low(void)
{
    HAL_GPIO_WritePin(GPIOB, WAPI_WAKE_Pin, GPIO_PIN_RESET);
}

/* -------------------------------------------------------------------------- */
/*                        Low-power enter / exit                              */
/* -------------------------------------------------------------------------- */

void bsp_wapi_enter_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Disable LPUART1 IDLE before reconfiguring PB10/11 so the floating RX pin
     * cannot flag a spurious IDLE during reconfig. */
    __HAL_UART_DISABLE_IT(&hlpuart1, UART_IT_IDLE);
    __HAL_UART_CLEAR_FLAG(&hlpuart1, UART_CLEAR_PEF | UART_CLEAR_FEF |
                                     UART_CLEAR_NEF | UART_CLEAR_OREF |
                                     UART_CLEAR_IDLEF);

    /* GPIOA: PA12 (WAPI_PWR) → analog */
    GPIO_InitStruct.Pin  = WAPI_PWR_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* GPIOB: PB10/11 (LPUART1) + PB13 (WAPI_WAKE) → analog */
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11 | WAPI_WAKE_Pin;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void bsp_wapi_exit_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA12: WAPI_PWR, push-pull output */
    GPIO_InitStruct.Pin   = WAPI_PWR_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB13: WAPI_WAKE, open-drain output */
    GPIO_InitStruct.Pin   = WAPI_WAKE_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PB10/PB11: LPUART1_RX/TX, AF8 */
    GPIO_InitStruct.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF8_LPUART1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Clear any flags that latched during reconfig, then re-enable IDLE. */
    __HAL_UART_CLEAR_FLAG(&hlpuart1, UART_CLEAR_PEF | UART_CLEAR_FEF |
                                     UART_CLEAR_NEF | UART_CLEAR_OREF |
                                     UART_CLEAR_IDLEF);
    __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_IDLE);
}
