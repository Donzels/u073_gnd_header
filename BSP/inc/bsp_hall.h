/**
 * @file bsp_hall.h
 * @brief BSP Hall sensor + EXTI control.
 *
 * Wraps reading the WKUP_EXTI pin level and clearing EXTI pending bits.
 */

#ifndef __BSP_HALL_H__
#define __BSP_HALL_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read the current level of the WKUP_EXTI (hall2) pin.
 * @return true if high, false if low.
 */
bool bsp_hall_wkup_read(void);

/**
 * @brief Clear the EXTI pending bits for HALL_EXTI and WKUP_EXTI.
 */
void bsp_hall_exti_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_HALL_H__ */
