#ifndef __WAPI_COMMU_H__
#define __WAPI_COMMU_H__

#include "WAPI_M0804C.h"
#include "osal.h"
#include "upper_commu.h"
#include "switch_handler.h"
#include "rfid_handler.h"



extern m0804c_handler_t g_wapi_handler_inst;

#include "dbg_console.h"
/* #include "SEGGER_RTT.h"                                                              */
/* extern int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...);        */
/* Original RTT-based macros (kept, commented):                                         */
/* #define WAPI_COMMU_DEBUG_OUT(fmt, ...)      SEGGER_RTT_printf(0, fmt, ##__VA_ARGS__) */
/* #define WAPI_COMMU_DEBUG_ERR(fmt, ...)      SEGGER_RTT_printf(0, RTT_CTRL_TEXT_BRIGHT_RED fmt RTT_CTRL_RESET "\r\n", ##__VA_ARGS__) */
/* #define WAPI_COMMU_DEBUG_STRING(p_data, len) */
/*     do { */
/*         SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_BRIGHT_RED); */
/*         SEGGER_RTT_Write(0, (const char*)p_data, len); */
/*         SEGGER_RTT_WriteString(0, RTT_CTRL_RESET "\r\n"); */
/*     } while(0) */
#define WAPI_COMMU_DEBUG_OUT(fmt, ...)      dbg_console_printf(fmt, ##__VA_ARGS__)
#define WAPI_COMMU_DEBUG_ERR(fmt, ...)      dbg_console_printf(fmt "\r\n", ##__VA_ARGS__)
#define WAPI_COMMU_DEBUG_STRING(p_data, len)  \
    do {                                      \
        dbg_console_write((const char*)(p_data), (len)); \
        dbg_console_write("\r\n", 2);          \
    } while(0)

#define SLEEPTIME_COMMU_MS              (90*1000)
#define SLEEPTIME_CONFIG_MS             (3*60*1000)
#define SLEEPTIME_COMPLETION_MS         (5*1000)

#define WAPI_COMMU_PARSE_THREAD_STACK_DEPTH        2048
#define WAPI_COMMU_PARSE_THREAD_PRIORITY           24 /* WAPI_COMMU_PARSE_THREAD_PRIORITY must higher than UPP_COMMU_PARSE_THREAD_PRIORITY */

/**
 * @brief Query the current WAPI config state.
 *
 * Reflects how far the configuration-mode flow (wapi_config_init) has progressed:
 *   CONFIG_STATE_NOT_CALLED  – wapi_config_init() has never been invoked.
 *   CONFIG_STATE_CONFIGURING – wapi_config_init() was called, but the config-mode
 *                              connection (PROCESS_CONNECT) has not yet succeeded.
 *   CONFIG_STATE_SUCCESS     – wapi_process_cfg_success_cb ran with PROCESS_CONNECT,
 *                              i.e. the config-mode connection was established.
 *   CONFIG_STATE_FAILED      – wapi_process_cfg_fail_cb ran with PROCESS_CONNECT,
 *                              i.e. the config-mode connection failed.
 */
typedef enum
{
    CONFIG_STATE_NOT_CALLED = 0,
    CONFIG_STATE_CONFIGURING,
    CONFIG_STATE_SUCCESS,
    CONFIG_STATE_FAILED
} wapi_config_state_t;

void wapi_commu_init(void);
void wapi_config_init(pf_at_recv_parse_t recv_parse_cb);
wapi_status_t wapi_send(uint8_t *buf, uint16_t length);

wapi_config_state_t wapi_query_config_state(void);
void wapi_set_config_state(wapi_config_state_t state);

void wapi_close(void);

/* -------------------------------------------------------------------------- */
/*                        WAPI communication state                            */
/* -------------------------------------------------------------------------- */

typedef enum
{
    COMMU_STATE_NOT_CALLED = 0,
    COMMU_STATE_RUNNING,
    COMMU_STATE_SUCCESS,
    COMMU_STATE_FAILED
} wapi_commu_state_t;

wapi_commu_state_t wapi_query_commu_state(void);
void wapi_set_commu_state(wapi_commu_state_t state);

/* -------------------------------------------------------------------------- */
/*                    Hall2 edge event queue (hall2 -> wapi_commu_task)        */
/* -------------------------------------------------------------------------- */

/* Payload carried from hall2_edge_event_cb() to wapi_commu_task. */
typedef struct
{
    switch_edge_t edge;          /* which edge was detected                 */
    uint32_t      tick;          /* HAL_GetTick() at detection time         */
    uint8_t       rfid[RFID_DATA_LEN]; /* decoded tag, or all-0xFF if unread  */
} hall2_edge_event_t;

/*
 * Post a hall2 edge event together with an RFID tag to wapi_commu_task.
 * Used by the hall2 -> rfid_read_asy retry path: edge + rfid (all-0xFF if every
 * attempt failed) are forwarded as one event. Non-blocking, see above.
 */
wapi_status_t wapi_commu_post_hall2_edge_rfid(switch_edge_t edge, const uint8_t *rfid);

/* -------------------------------------------------------------------------- */
/*                    Low-power enter / exit hooks                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prepare the WAPI communication module for low-power sleep.
 *
 * No-op on the U0: M0804C state is retained across Stop 2. Present for
 * symmetry / future use.
 */
void wapi_commu_enter_low_power(void);

/**
 * @brief Restore the WAPI communication module after waking from low-power sleep.
 *
 * No-op on the U0. Present for symmetry / future use.
 */
void wapi_commu_exit_low_power(void);

#endif /* __WAPI_COMMU_H__ */
