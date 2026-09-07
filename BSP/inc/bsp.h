/**
 * @file bsp.h
 * @brief BSP (Board Support Package) master include.
 *
 * Wraps all direct chip / HAL accesses behind application-meaningful APIs.
 * APP code calls bsp_xxx_yyy() functions — never HAL_xxx() directly.
 */

#ifndef __BSP_H__
#define __BSP_H__

#include "bsp_led.h"
#include "bsp_rfid.h"
#include "bsp_wapi.h"
#include "bsp_hall.h"
#include "bsp_system.h"
#include "bsp_adc.h"
#include "bsp_pvd.h"
#include "bsp_rtc.h"
#include "bsp_flash.h"

#endif /* __BSP_H__ */
