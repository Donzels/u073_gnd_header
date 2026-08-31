#ifndef __UPPER_COMMU_H__
#define __UPPER_COMMU_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "uart_proto.h"
#include "agile_modbus.h"
#include "osal.h"
#include "WAPI_M0804C.h"
#include "stroage_handler.h"
#include "algo_data_integrity.h"

#define WAPI_CFG_BY_WIRED       0
#define WAPI_CFG_BY_WIRELESS    1
#define WAPI_CFG_MODE           WAPI_CFG_BY_WIRELESS
#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRELESS
extern recv_buf_att_t g_wapi_uart_rx_buf;
#endif

#include "dbg_console.h"
/* #include "SEGGER_RTT.h"                                                       */
/* extern int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...); */
/* #define UP_COMMU_DEBUG_OUT(fmt, ...)  SEGGER_RTT_printf(0, fmt, ##__VA_ARGS__) */
#define UP_COMMU_DEBUG_OUT(fmt, ...)      dbg_console_printf(fmt, ##__VA_ARGS__)   /* Output log to debug console */
#define UP_COMMU_DEBUG_WARN(fmt, ...)      SEGGER_RTT_printf(0, RTT_CTRL_TEXT_BRIGHT_RED fmt RTT_CTRL_RESET "\r\n", ##__VA_ARGS__)

#define UPP_COMMU_PARSE_THREAD_STACK_DEPTH        2048
#define UPP_COMMU_PARSE_THREAD_PRIORITY           25 /* WAPI_COMMU_PARSE_THREAD_PRIORITY must higher than UPP_COMMU_PARSE_THREAD_PRIORITY */

extern m0804c_handler_t g_wapi_handler_inst;


void upper_commu_init(void);

#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
/* call in IDLE ISR */
void notify_upp_commu_recv_isr_cb(void);
void reset_upp_uart(void);
#else
void wapi_config(void);
#endif

wapi_info_t *get_wapi_info(void);
cert_file_t *get_cert_file(void);


wapi_info_t *get_wapi_cfg_info(void);

/* -------------------------------------------------------------------------- */
/*                    Low-power enter / exit hooks                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prepare the upper-communication module for low-power sleep.
 *
 * No-op on the U0: Modbus state is retained across Stop 2. Present for
 * symmetry / future use.
 */
void upper_commu_enter_low_power(void);

/**
 * @brief Restore the upper-communication module after waking from low-power sleep.
 *
 * No-op on the U0. Present for symmetry / future use.
 */
void upper_commu_exit_low_power(void);

#endif//__UPPER_COMMU_H__

