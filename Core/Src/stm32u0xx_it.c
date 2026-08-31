/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32u0xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32u0xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "WAPI_M0804C.h"
#include "upper_commu.h"
#include "switch_handler.h"
#include "rfid.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
 extern m0804c_handler_t g_wapi_handler_inst;
/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_lpuart1_rx;
extern DMA_HandleTypeDef hdma_lpuart1_tx;
extern UART_HandleTypeDef hlpuart1;
extern RTC_HandleTypeDef hrtc;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim15;
extern TIM_HandleTypeDef htim7;

/* USER CODE BEGIN EV */
#include "dbg_console.h"
#define IT_DEBUG_OUT(fmt, ...)      dbg_console_printf(fmt, ##__VA_ARGS__) 
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  /* megenta */
    HAL_GPIO_WritePin(GPIOA, LED_GREEN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LED_RED_Pin | LED_BLUE_Pin, GPIO_PIN_RESET);
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/******************************************************************************/
/* STM32U0xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32u0xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles PVD through EXTI Line detection Interrupt(EXTI lines 16/19/20/21).
  */
void PVD_PVM_IRQHandler(void)
{
  /* USER CODE BEGIN PVD_PVM_IRQn 0 */

  /* USER CODE END PVD_PVM_IRQn 0 */
  HAL_PWREx_PVD_PVM_IRQHandler();
  /* USER CODE BEGIN PVD_PVM_IRQn 1 */

  /* USER CODE END PVD_PVM_IRQn 1 */
}

/**
  * @brief This function handles RTC and TAMP interrupts (combined EXTI lines 20 & 21).
  */
void RTC_TAMP_IRQHandler(void)
{
  /* USER CODE BEGIN RTC_TAMP_IRQn 0 */

  /* USER CODE END RTC_TAMP_IRQn 0 */
  HAL_RTC_AlarmIRQHandler(&hrtc);
  HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
  /* USER CODE BEGIN RTC_TAMP_IRQn 1 */

  /* USER CODE END RTC_TAMP_IRQn 1 */
}

/**
  * @brief This function handles EXTI line 0 and line 1 interrupts.
  */
void EXTI0_1_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_1_IRQn 0 */

  /* USER CODE END EXTI0_1_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(WKUP_EXTI_Pin);
  /* USER CODE BEGIN EXTI0_1_IRQn 1 */

  /* USER CODE END EXTI0_1_IRQn 1 */
}

/**
  * @brief This function handles EXTI line 4 to 15 interrupts.
  */
void EXTI4_15_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI4_15_IRQn 0 */

  /* USER CODE END EXTI4_15_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(HALL_EXTI_Pin);
  /* USER CODE BEGIN EXTI4_15_IRQn 1 */

  /* USER CODE END EXTI4_15_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel 1 interrupt.
  */
void DMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel1_IRQn 0 */

  /* USER CODE END DMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_lpuart1_rx);
  /* USER CODE BEGIN DMA1_Channel1_IRQn 1 */

  /* USER CODE END DMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel 2 and channel 3 interrupts.
  */
void DMA1_Channel2_3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel2_3_IRQn 0 */

  /* USER CODE END DMA1_Channel2_3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_lpuart1_tx);
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA1_Channel2_3_IRQn 1 */

  /* USER CODE END DMA1_Channel2_3_IRQn 1 */
}

/**
  * @brief This function handles ADC1, COMP1 and COMP2 Interrupts (combined with EXTI 17 & 18).
  */
void ADC_COMP1_2_IRQHandler(void)
{
  /* USER CODE BEGIN ADC_COMP1_2_IRQn 0 */

  /* USER CODE END ADC_COMP1_2_IRQn 0 */
  HAL_ADC_IRQHandler(&hadc1);
  /* USER CODE BEGIN ADC_COMP1_2_IRQn 1 */

  /* USER CODE END ADC_COMP1_2_IRQn 1 */
}

/**
  * @brief This function handles TIM2 Global Interrupt.
  */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */

  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */

  /* USER CODE END TIM2_IRQn 1 */
}

/**
  * @brief This function handles TIM7 and LPTIM2 global Interrupt (combined with EXTI 32).
  */
void TIM7_LPTIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM7_LPTIM2_IRQn 0 */

  /* USER CODE END TIM7_LPTIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim7);
  /* USER CODE BEGIN TIM7_LPTIM2_IRQn 1 */

  /* USER CODE END TIM7_LPTIM2_IRQn 1 */
}

/**
  * @brief This function handles TIM15 + LPTIM3 global interrupt (combined with EXTI 33).
  */
void TIM15_LPTIM3_IRQHandler(void)
{
  /* USER CODE BEGIN TIM15_LPTIM3_IRQn 0 */
  // IT_DEBUG_OUT("1\r\n");
    if (__HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC1) || __HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC2))
    {
        ic_isr_cb();
        __HAL_TIM_CLEAR_FLAG(&htim15, TIM_FLAG_CC1 | TIM_FLAG_CC2);
    }
    // if (__HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC1))
    // {
    //     ic_isr_cb();
    //     __HAL_TIM_CLEAR_FLAG(&htim15, TIM_FLAG_CC1);
    // }
    // if (__HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC2))
    // {
    //     ic_isr_cb();
    //     __HAL_TIM_CLEAR_FLAG(&htim15, TIM_FLAG_CC2);
    // }
  /* USER CODE END TIM15_LPTIM3_IRQn 0 */
  HAL_TIM_IRQHandler(&htim15);
  /* USER CODE BEGIN TIM15_LPTIM3_IRQn 1 */

  /* USER CODE END TIM15_LPTIM3_IRQn 1 */
}

/**
  * @brief This function handles USART3 (combined with EXTI 24) + LPUART1 global interrupt (combined with EXTI lines 28).
  */
void USART3_LPUART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART3_LPUART1_IRQn 0 */
#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED  
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart3);
        notify_upp_commu_recv_isr_cb();
    }
#endif
      if (__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&hlpuart1);
        m0804c_at_notify_recv_isr_cb(&g_wapi_handler_inst);
    }   
  /* USER CODE END USART3_LPUART1_IRQn 0 */
  HAL_UART_IRQHandler(&hlpuart1);
  /* USER CODE BEGIN USART3_LPUART1_IRQn 1 */

  /* USER CODE END USART3_LPUART1_IRQn 1 */
}

/* USER CODE BEGIN 1 */

extern switch_handler_t g_hall_handler_inst;
extern switch_handler_t g_hall2_handler_inst;

/**
 * @brief EXTI rising-edge callback — forwards pin release to the
 *        corresponding switch handler.
 */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == HALL_EXTI_Pin)
    {
        switch_handler_rising_callback(&g_hall_handler_inst);
    }
    else if (GPIO_Pin == WKUP_EXTI_Pin)
    {
        switch_handler_rising_callback(&g_hall2_handler_inst);
    }
}

/**
 * @brief EXTI falling-edge callback — forwards pin assertion to the
 *        corresponding switch handler.
 */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == HALL_EXTI_Pin)
    {
        switch_handler_falling_callback(&g_hall_handler_inst);
    }
    else if (GPIO_Pin == WKUP_EXTI_Pin)
    {
        switch_handler_falling_callback(&g_hall2_handler_inst);
    }
}

/* USER CODE END 1 */
