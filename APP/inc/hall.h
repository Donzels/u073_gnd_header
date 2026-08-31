#ifndef __HALL_H__
#define __HALL_H__

#include "switch_handler.h"

/**
 * @brief Initialise both hall1 (PRESS mode) and hall2 (EDGE mode) and register
 *        the application callbacks.
 *
 * Called once during system startup (from App_ThreadX_Init).
 */
void hall_init(void);

/**
 * @brief Hall1 (HALL_EXTI) press-event callback — PRESS mode.
 *
 * Called in the handler thread context whenever a short or long press is
 * classified on the HALL_EXTI pin.
 *
 * @param press_type  SWITCH_PRESS_SHORT or SWITCH_PRESS_LONG.
 */
void hall_press_event_cb(switch_press_type_t press_type);

/* -------------------------------------------------------------------------- */
/*                    Low-power enter / exit hooks                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prepare the hall module for low-power sleep.
 *
 * Called by the power-management thread before entering Stop 2. Disables
 * EXTI interrupts so the hall pins do not spuriously wake the device (the
 * wakeup source is configured separately if needed).
 */
void hall_enter_low_power(void);

/**
 * @brief Restore the hall module after waking from low-power sleep.
 *
 * Re-enables EXTI interrupts so edge detection resumes.
 */
void hall_exit_low_power(void);

/**
 * @brief Sample the hall2 (WKUP_EXTI) pin level and fire the matching edge callback.
 *
 * Reads the current level of the WKUP_EXTI pin and synthesises a falling or
 * rising edge callback on g_hall2_handler_inst. Useful after a periodic RTC
 * wakeup where no EXTI fired but the application still needs to react to the
 * current hall2 state.
 */
void hall2_sample_edge(void);

#endif /* __HALL_H__ */
