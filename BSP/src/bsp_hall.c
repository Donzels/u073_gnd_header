/**
 * @file bsp_hall.c
 * @brief BSP Hall sensor implementation — WKUP_EXTI read + EXTI clear.
 */

#include "bsp_hall.h"
#include "main.h"

bool bsp_hall_wkup_read(void)
{
    return (HAL_GPIO_ReadPin(WKUP_EXTI_GPIO_Port, WKUP_EXTI_Pin) == GPIO_PIN_SET);
}

void bsp_hall_exti_clear(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(HALL_EXTI_Pin | WKUP_EXTI_Pin);
}
