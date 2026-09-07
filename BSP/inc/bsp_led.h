/**
 * @file bsp_led.h
 * @brief BSP LED control — wraps the GPIO operations for the RGB LED.
 *
 * APP calls bsp_led_red_on() etc. directly. No HAL_GPIO_* appears in APP.
 */

#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Individual colour on (active-low: other colours SET, target RESET) ---- */
void bsp_led_red_on(void);
void bsp_led_green_on(void);
void bsp_led_blue_on(void);

/* ---- Composite colours ---- */
void bsp_led_yellow_on(void);    /* red + green */
void bsp_led_magenta_on(void);   /* red + blue  */
void bsp_led_cyan_on(void);      /* green + blue */

/* ---- All off (all pins SET) ---- */
void bsp_led_all_off(void);

/* ---- Low-power: analog (no pull) to eliminate GPIO leakage ---- */
void bsp_led_enter_low_power(void);

/* ---- Wakeup: restore open-drain output, all off ---- */
void bsp_led_exit_low_power(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LED_H__ */
