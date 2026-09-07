/**
 * @file bsp_pvd.h
 * @brief BSP PVD (Programmable Voltage Detector) control.
 *
 * Wraps HAL_PWR_ConfigPVD / EnablePVD / DisablePVD and the
 * HAL_PWR_PVDCallback override. APP registers its event handler via
 * bsp_pvd_register_cb() so APP never writes a HAL callback directly.
 */

#ifndef __BSP_PVD_H__
#define __BSP_PVD_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PVD event callback.
 * @param low  true if VDD fell below threshold, false if recovered.
 */
typedef void (*bsp_pvd_cb_t)(bool low);

/**
 * @brief PVD threshold levels (maps to STM32U073 PWR_PVDLEVEL_0…6).
 */
typedef enum
{
    BSP_PVD_LEVEL_2_0V = 0,
    BSP_PVD_LEVEL_2_2V,
    BSP_PVD_LEVEL_2_4V,
    BSP_PVD_LEVEL_2_5V,
    BSP_PVD_LEVEL_2_6V,
    BSP_PVD_LEVEL_2_8V,
    BSP_PVD_LEVEL_2_9V,
} bsp_pvd_level_t;

/* ---- Configuration ---- */
void bsp_pvd_init(bsp_pvd_level_t level);
void bsp_pvd_enable(void);
void bsp_pvd_disable(void);

/**
 * @brief Read the live PVD output flag.
 * @return true if VDD is below threshold, false otherwise.
 */
bool bsp_pvd_is_low(void);

/* ---- Callback registration ---- */
void bsp_pvd_register_cb(bsp_pvd_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PVD_H__ */
