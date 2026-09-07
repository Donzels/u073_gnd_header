/**
 * @file bsp_led.c
 * @brief BSP LED implementation — wraps GPIO for the RGB LED (active-low).
 */

#include "bsp_led.h"
#include "main.h"

/* Active-low LED: SET = off, RESET = on. */

void bsp_led_red_on(void)
{
    HAL_GPIO_WritePin(GPIOA, LED_GREEN_Pin | LED_BLUE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin, GPIO_PIN_RESET);
}

void bsp_led_green_on(void)
{
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin | LED_BLUE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LED_GREEN_Pin, GPIO_PIN_RESET);
}

void bsp_led_blue_on(void)
{
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin | LED_GREEN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LED_BLUE_Pin, GPIO_PIN_RESET);
}

void bsp_led_yellow_on(void)
{
    HAL_GPIO_WritePin(GPIOA, LED_BLUE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin | LED_GREEN_Pin, GPIO_PIN_RESET);
}

void bsp_led_magenta_on(void)
{
    HAL_GPIO_WritePin(GPIOA, LED_GREEN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin | LED_BLUE_Pin, GPIO_PIN_RESET);
}

void bsp_led_cyan_on(void)
{
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LED_GREEN_Pin | LED_BLUE_Pin, GPIO_PIN_RESET);
}

void bsp_led_all_off(void)
{
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin | LED_GREEN_Pin | LED_BLUE_Pin, GPIO_PIN_SET);
}

/* ---- Low-power: analog (no pull) to eliminate GPIO leakage ---- */
void bsp_led_enter_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = LED_RED_Pin | LED_GREEN_Pin | LED_BLUE_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* ---- Wakeup: restore open-drain output, all off ---- */
void bsp_led_exit_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = LED_RED_Pin | LED_GREEN_Pin | LED_BLUE_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    /* Active-low LEDs: SET = off. */
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin | LED_GREEN_Pin | LED_BLUE_Pin, GPIO_PIN_SET);
}
