/**
 * @file bsp_pvd.c
 * @brief BSP PVD implementation — configure / enable / disable + callback.
 *
 * The HAL_PWR_PVDCallback override lives here. APP registers its event handler
 * via bsp_pvd_register_cb().
 */

#include "bsp_pvd.h"
#include "main.h"
#include "stm32u0xx_ll_pwr.h"   /* LL_PWR_IsActiveFlag_PVDO() */

/* ---- Application callback (NULL if none) ---- */
static bsp_pvd_cb_t s_pvd_cb = NULL;

/* ---- Threshold table (maps enum → HAL PWR_PVDLEVEL_x) ---- */
static const uint32_t s_pvd_level_table[] =
{
    [BSP_PVD_LEVEL_2_0V] = PWR_PVDLEVEL_0,
    [BSP_PVD_LEVEL_2_2V] = PWR_PVDLEVEL_1,
    [BSP_PVD_LEVEL_2_4V] = PWR_PVDLEVEL_2,
    [BSP_PVD_LEVEL_2_5V] = PWR_PVDLEVEL_3,
    [BSP_PVD_LEVEL_2_6V] = PWR_PVDLEVEL_4,
    [BSP_PVD_LEVEL_2_8V] = PWR_PVDLEVEL_5,
    [BSP_PVD_LEVEL_2_9V] = PWR_PVDLEVEL_6,
};

/* -------------------------------------------------------------------------- */
/*                        Configuration                                        */
/* -------------------------------------------------------------------------- */

void bsp_pvd_init(bsp_pvd_level_t level)
{
    uint32_t pvd_level = s_pvd_level_table[(uint32_t)level];

    PWR_PVDTypeDef pvd_config =
    {
        .PVDLevel = pvd_level,
        .Mode     = PWR_PVD_MODE_IT_RISING_FALLING,
    };
    HAL_PWR_ConfigPVD(&pvd_config);

    /* Clear any stale EXTI flag so the first event reflects the real state. */
    __HAL_PWR_PVD_EXTI_CLEAR_FLAG();
}

void bsp_pvd_enable(void)
{
    HAL_PWR_EnablePVD();
}

void bsp_pvd_disable(void)
{
    HAL_PWR_DisablePVD();
}

bool bsp_pvd_is_low(void)
{
    return (LL_PWR_IsActiveFlag_PVDO() != 0U);
}

/* -------------------------------------------------------------------------- */
/*                        Callback registration                                */
/* -------------------------------------------------------------------------- */

void bsp_pvd_register_cb(bsp_pvd_cb_t cb)
{
    s_pvd_cb = cb;
}

/* -------------------------------------------------------------------------- */
/*                        HAL callback override                                */
/* -------------------------------------------------------------------------- */

void HAL_PWR_PVDCallback(void)
{
    if (s_pvd_cb == NULL)
    {
        return;
    }

    /* PVDO: 1 = VDD below threshold, 0 = VDD at/above threshold. */
    bool low = (LL_PWR_IsActiveFlag_PVDO() != 0U);
    s_pvd_cb(low);
}
