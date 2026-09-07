/**
 * @file bsp_rfid.h
 * @brief BSP RFID hardware control — GPIO + TIM PWM/input-capture.
 *
 * Wraps the RFID power, sleep, PWM carrier (TIM2_CH3), and input-capture
 * (TIM15_CH1/CH2) operations so APP never touches HAL_TIM_* or the RFID
 * GPIOs directly.
 */

#ifndef __BSP_RFID_H__
#define __BSP_RFID_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Edge type (matches handler's capture_edge_t: EDGE_RISING=0, EDGE_FALLING, EDGE_ERR) ---- */
typedef enum
{
    BSP_RFID_EDGE_RISING = 0,
    BSP_RFID_EDGE_FALLING,
    BSP_RFID_EDGE_ERR,
} bsp_rfid_edge_t;

/* ---- Power / sleep / carrier control ---- */
void bsp_rfid_pwr_on(void);          /* RFID_PWR = SET   (active high)  */
void bsp_rfid_pwr_off(void);         /* RFID_PWR = RESET */
void bsp_rfid_sleep_on(void);        /* RFID_SLEEP = SET  (active high)  */
void bsp_rfid_sleep_off(void);       /* RFID_SLEEP = RESET */
void bsp_rfid_pwm_n_high(void);      /* PWM_N = SET  (active high) */
void bsp_rfid_pwm_n_low(void);       /* PWM_N = RESET */

/* ---- 125 kHz carrier PWM (TIM2_CH3) ---- */
void bsp_rfid_carrier_start(void);
void bsp_rfid_carrier_stop(void);

/* ---- Input capture (TIM15_CH1 = rising, TIM15_CH2 = falling) ---- */
void bsp_rfid_ic_start(void);
void bsp_rfid_ic_stop(void);

/* ---- Capture helpers (read flags / value / ARR) ---- */
bsp_rfid_edge_t bsp_rfid_get_capture_edge(void);
uint32_t        bsp_rfid_get_ic_value(bsp_rfid_edge_t edge);
uint32_t        bsp_rfid_get_arr(void);

/* ---- Low-power: stop carrier + IC, set pins to analog ---- */
void bsp_rfid_enter_low_power(void);

/* ---- Wakeup: restore pins to functional config ---- */
void bsp_rfid_exit_low_power(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RFID_H__ */
