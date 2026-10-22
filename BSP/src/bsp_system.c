/**
 * @file bsp_system.c
 * @brief BSP system helpers — tick, UID, watchdog, system power, Stop 2.
 */

#include "bsp_system.h"
#include "main.h"

/* Defined in main.c (CubeMX-generated) */
extern void SystemClock_Config(void);

/* -------------------------------------------------------------------------- */
/*                        Time                                                */
/* -------------------------------------------------------------------------- */

uint32_t bsp_get_tick(void)
{
    return HAL_GetTick();
}

/* -------------------------------------------------------------------------- */
/*                        Unique ID (96 bits → 6 bytes)                       */
/* -------------------------------------------------------------------------- */

void bsp_get_uid(uint8_t *uid_6bytes)
{
    uint32_t w0 = HAL_GetUIDw0();
    uint32_t w1 = HAL_GetUIDw1();
    uint32_t w2 = HAL_GetUIDw2();

    uid_6bytes[0] = (uint8_t)(w0 >> 0);
    uid_6bytes[1] = (uint8_t)(w0 >> 8);
    uid_6bytes[2] = (uint8_t)(w1 >> 0);
    uid_6bytes[3] = (uint8_t)(w1 >> 8);
    uid_6bytes[4] = (uint8_t)(w2 >> 0);
    uid_6bytes[5] = (uint8_t)(w2 >> 8);
}

/* -------------------------------------------------------------------------- */
/*                        External watchdog (WATCH_DOG_OUT)                    */
/* -------------------------------------------------------------------------- */

void bsp_wdog_toggle(void)
{
    HAL_GPIO_TogglePin(WATCH_DOG_OUT_GPIO_Port, WATCH_DOG_OUT_Pin);
}

void bsp_wdog_set_high(void)
{
    HAL_GPIO_WritePin(GPIOB, WATCH_DOG_OUT_Pin, GPIO_PIN_SET);
}

void bsp_wdog_set_low(void)
{
    HAL_GPIO_WritePin(GPIOB, WATCH_DOG_OUT_Pin, GPIO_PIN_RESET);
}

void bsp_wdog_feed(void)
{
    bsp_wdog_set_high();
    __NOP(); __NOP(); __NOP();
    bsp_wdog_set_low();
}

/* -------------------------------------------------------------------------- */
/*                        System power switch (SYS_POWER_SW)                   */
/* -------------------------------------------------------------------------- */

void bsp_sys_pwr_set_high(void)
{
    HAL_GPIO_WritePin(GPIOB, SYS_POWER_SW_Pin, GPIO_PIN_SET);
}

void bsp_sys_pwr_set_low(void)
{
    HAL_GPIO_WritePin(GPIOB, SYS_POWER_SW_Pin, GPIO_PIN_RESET);
}

/* -------------------------------------------------------------------------- */
/*                        Low-power: system + debug pins → analog              */
/* -------------------------------------------------------------------------- */

void bsp_sys_enter_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIOB: PB7 (SYS_POWER_SW) → analog, no pull */
    GPIO_InitStruct.Pin  = SYS_POWER_SW_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    bsp_wdog_set_low();
}

void bsp_sys_exit_low_power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PB7: SYS_POWER_SW, push-pull output */
    GPIO_InitStruct.Pin       = SYS_POWER_SW_Pin;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0U;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    bsp_sys_pwr_set_high();

    bsp_wdog_set_low();
}

/* -------------------------------------------------------------------------- */
/*                        Enter Stop 2 (WFI) with full preparation            */
/* -------------------------------------------------------------------------- */

void bsp_enter_stop2(void)
{
    /* Stop SysTick and disable TIM7/LPTIM2 (direct register access). */
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    NVIC_DisableIRQ(TIM7_LPTIM2_IRQn);

    /* Clear pending system interrupts so they don't spuriously wake the device. */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;  /* SysTick */
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;  /* PendSV */
    NVIC_ClearPendingIRQ(TIM7_LPTIM2_IRQn);

    /* Enter Stop 2 (WFI). */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* After wakeup: reconfigure clocks, re-enable SysTick and TIM7/LPTIM2. */
    SystemClock_Config();
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    NVIC_EnableIRQ(TIM7_LPTIM2_IRQn);
}
