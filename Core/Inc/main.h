/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "dbg_console.h"
/* #include "SEGGER_RTT.h"                                                       */
/* extern int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...); */
/* #define MAIN_DEBUG_OUT(fmt, ...)      SEGGER_RTT_printf(0, fmt, ##__VA_ARGS__)*/
/* #define MAIN_DEBUG_GREEN_STRING(p_data, len) */
/*     do { */
/*         SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_BRIGHT_GREEN); */
/*         SEGGER_RTT_Write(0, (const char*)p_data, len); */
/*         SEGGER_RTT_WriteString(0, RTT_CTRL_RESET); */
/*     } while(0) */
#define MAIN_DEBUG_OUT(fmt, ...)      dbg_console_printf(fmt, ##__VA_ARGS__)  /* Output log to debug console */
#define MAIN_DEBUG_GREEN_STRING(p_data, len)  \
    do {                                  \
        dbg_console_write((const char*)(p_data), (len)); \
        dbg_console_write("\r\n", 2);     \
    } while(0) 
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define WKUP_EXTI_Pin GPIO_PIN_0
#define WKUP_EXTI_GPIO_Port GPIOA
#define WKUP_EXTI_EXTI_IRQn EXTI0_1_IRQn
#define LED_BLUE_Pin GPIO_PIN_1
#define LED_BLUE_GPIO_Port GPIOA
#define PWM_N_Pin GPIO_PIN_3
#define PWM_N_GPIO_Port GPIOA
#define RFID_PWR_Pin GPIO_PIN_4
#define RFID_PWR_GPIO_Port GPIOA
#define RFID_SLEEP_Pin GPIO_PIN_5
#define RFID_SLEEP_GPIO_Port GPIOA
#define LED_RED_Pin GPIO_PIN_6
#define LED_RED_GPIO_Port GPIOA
#define LED_GREEN_Pin GPIO_PIN_7
#define LED_GREEN_GPIO_Port GPIOA
#define WAPI_WAKE_Pin GPIO_PIN_13
#define WAPI_WAKE_GPIO_Port GPIOB
#define HALL_EXTI_Pin GPIO_PIN_11
#define HALL_EXTI_GPIO_Port GPIOA
#define HALL_EXTI_EXTI_IRQn EXTI4_15_IRQn
#define WAPI_PWR_Pin GPIO_PIN_12
#define WAPI_PWR_GPIO_Port GPIOA
#define WATCH_DOG_OUT_Pin GPIO_PIN_4
#define WATCH_DOG_OUT_GPIO_Port GPIOB
#define SYS_POWER_SW_Pin GPIO_PIN_7
#define SYS_POWER_SW_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* --------------------------------------------------------------------------
 * WAPI module firmware-upgrade test mode.
 * Define WAPI_UPGRADE_TEST to build a firmware that ONLY runs the WAPI
 * bootloader-entry sequence (no RTOS tasks are started):
 *   1. Power on the WAPI module
 *   2. Pull PB12 high (boot/upgrade pin)
 *   3. Power off the WAPI module for 1 s
 *   4. Power on the WAPI module again
 * Comment it out to build the normal application.
 * ------------------------------------------------------------------------ */
// #define WAPI_UPGRADE_TEST

/* PB12: WAPI module boot/upgrade pin (not routed in CubeMX). */
#define WAPI_UPGRADE_BOOT_Pin       GPIO_PIN_12
#define WAPI_UPGRADE_BOOT_GPIO_Port GPIOB

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
