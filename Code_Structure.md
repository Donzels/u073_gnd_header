# Code Structure Tree Diagram

## Layered Architecture Overview

```
+-----------------------------------------------------------------------------+
|                          APPLICATION LAYER                                  |
|   Pure business logic; calls BSP APIs only. No HAL/LL/register access.      |
+-----------------------------------------------------------------------------+
|                          HANDLER LAYER                                      |
|   State machines, algorithms, protocol parsing. Chip-agnostic.              |
|   Calls OSAL and its own ops tables; never touches hardware.                |
+-----------------------------------------------------------------------------+
|                          BSP (Board Support Package)                        |
|   Wraps all HAL/LL calls behind application-meaningful function names.      |
|   Only this layer (besides Core/Src/) may invoke HAL/LL.                    |
+-----------------------------------------------------------------------------+
|                          OS ABSTRACTION LAYER (OSAL)                        |
|   Unified OS API (threads, queues, timers, mutexes).                        |
+-----------------------------------------------------------------------------+
|                          HAL / LL (ST Driver Library)                       |
|   Chip-specific register access. Provided by STM32CubeMX / ST.              |
+-----------------------------------------------------------------------------+
```

---

## Detailed Call Graph

```
u073_gnd_header/
|
+-- APP/  (Application Layer)
|   |
|   +-- led.c
|   |   +-- handler/led_handler.c
|   |   +-- BSP/bsp_led.c
|   |
|   +-- hall.c
|   |   +-- handler/switch_handler.c
|   |   +-- BSP/bsp_hall.c
|   |   +-- BSP/bsp_system.c
|   |
|   +-- rfid.c
|   |   +-- handler/rfid_handler.c
|   |   +-- BSP/bsp_rfid.c
|   |
|   +-- wapi_commu.c
|   |   +-- handler/WAPI_M0804C.c
|   |   +-- BSP/bsp_wapi.c
|   |   +-- BSP/bsp_system.c
|   |
|   +-- voltage.c
|   |   +-- BSP/bsp_adc.c
|   |   +-- BSP/bsp_pvd.c
|   |
|   +-- low_power.c
|   |   +-- handler/switch_handler.c  (reference)
|   |   +-- BSP/bsp_rtc.c
|   |   +-- BSP/bsp_system.c
|   |
|   +-- upper_commu.c  (pure protocol, no BSP/handler dependency)
|
+-- handler/  (Handler Layer)
|   +-- led_handler.c
|   +-- switch_handler.c
|   +-- rfid_handler.c
|   +-- AT_handler.c
|   +-- WAPI_M0804C.c
|   +-- stroage_handler.c
|   |   +-- BSP/bsp_flash.c
|   +-- high_relia_handler.c
|
+-- BSP/  (Board Support Package)
|   +-- bsp_led.c       --> HAL_GPIO_WritePin, HAL_GPIO_Init
|   +-- bsp_hall.c      --> HAL_GPIO_ReadPin, __HAL_GPIO_EXTI_CLEAR_IT
|   +-- bsp_system.c    --> HAL_GetTick, HAL_GetUID, HAL_GPIO_*, HAL_PWREx_EnterSTOP2, SysTick/NVIC/SCB
|   +-- bsp_rfid.c      --> HAL_GPIO_*, HAL_TIM_PWM_*, HAL_TIM_IC_*, __HAL_TIM_*
|   +-- bsp_wapi.c      --> HAL_GPIO_*, __HAL_UART_*
|   +-- bsp_adc.c       --> HAL_ADCEx_Calibration_Start, HAL_ADC_Start/Stop_DMA
|   +-- bsp_pvd.c       --> HAL_PWR_Config/Enable/DisablePVD, LL_PWR_IsActiveFlag_PVDO
|   +-- bsp_rtc.c       --> HAL_RTC_*, HAL_RTCEx_*, __HAL_RTC_*
|   +-- bsp_flash.c     --> HAL_FLASH_Unlock, HAL_FLASHEx_Erase, HAL_FLASH_Program, HAL_FLASH_Lock
|
+-- 02_OS_Platform/  (OS Abstraction Layer)
|   +-- OS_Wrapper/
|   +-- OS_Implementation/
|   +-- OS_Adapter/
|
+-- Drivers/  (HAL / LL Driver Library)
    +-- STM32U0xx_HAL_Driver/
    +-- CMSIS/
```

---

## Layer Comparison Table

| Layer            | Folder              | Responsibility                        | Can Call                                    | Must Not Call          |
|------------------|---------------------|---------------------------------------|---------------------------------------------|------------------------|
| **Application**  | `APP/`              | Business logic                        | BSP API, Handler API, OSAL                  | HAL / LL / registers   |
| **Handler**      | `handler/`          | State machines, algorithms, protocol  | OSAL, own ops tables, APP callbacks, **BSP** | HAL / LL / registers   |
| **BSP**          | `BSP/`              | Wrap HAL/LL into meaningful functions | HAL / LL, `main.h` pin definitions, CMSIS   | APP (one-way only)     |
| **OSAL**         | `02_OS_Platform/`   | OS abstraction                        | OS implementation (ThreadX)                 | --                     |
| **Driver**       | `Drivers/`          | Chip-specific register access         | Hardware registers                          | --                     |

---
