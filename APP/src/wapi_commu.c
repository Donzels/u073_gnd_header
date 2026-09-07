#include "wapi_commu.h"
#include "led.h"
#include "bsp.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "low_power.h"
#include "protocol_high_relia.h"

#define ERROR_PENDING_TIMEOUT_MS              (5*1000)   /* ms to wait for a pending response before giving up */
#define WAPI_PROCESS_FAIL_MAX                 2
/* -------------------------------------------------------------------------- */
/*                        Forward declarations (OSAL)                         */
/* -------------------------------------------------------------------------- */

static void m0804c_open(struct m0804c_handler *const self);
static void m0804c_close(struct m0804c_handler *const self);

static void wapi_process_success_cb(struct m0804c_handler *const self, wapi_process_type_t process_type);
static void wapi_process_err_cb(struct m0804c_handler *const self, wapi_process_type_t process_type);

static wapi_info_t *m0804c_get_wapi_info(struct m0804c_handler *const self);
static wapi_info_t *m0804c_get_config_wapi_info(struct m0804c_handler *const self);
static cert_file_t *m0804c_get_cert_file(struct m0804c_handler *const self);

static void wapi_process_cfg_success_cb(struct m0804c_handler *const self, wapi_process_type_t process_type);
static void wapi_process_cfg_err_cb(struct m0804c_handler *const self, wapi_process_type_t process_type);

static int8_t wapi_send_data(const uint8_t *data, uint16_t len);
static int8_t get_unique_id_6bytes(uint8_t *uid);
static int8_t get_rfid_6bytes(uint8_t *rfid);

extern recv_buf_att_t g_wapi_uart_rx_buf;
extern uart_ops_t g_wapi_uart_ops;

static wapi_config_state_t g_wapi_config_state = CONFIG_STATE_NOT_CALLED;

static high_relia_handler_t g_high_relia_handler_inst = {0};
static uint8_t g_last_rfid_tag[6] = {0};  /* Global variable to store the last successfully read RFID tag. */

static high_relia_ops_t g_high_relia_ops =
{
    .pf_get_rfid = get_rfid_6bytes,/* config in sending */
    .pf_get_uid  = get_unique_id_6bytes,
    .pf_send     = wapi_send_data
};

/* -------------------------------------------------------------------------- */
/*                        WAPI communication state                            */
/* -------------------------------------------------------------------------- */

static wapi_commu_state_t g_wapi_commu_state = COMMU_STATE_NOT_CALLED;

wapi_commu_state_t wapi_query_commu_state(void)
{
    return g_wapi_commu_state;
}

void wapi_set_commu_state(wapi_commu_state_t state)
{
    g_wapi_commu_state = state;
}

static void wapi_config_fail(void)
{
    wapi_close();
    led_on_off(NETWORK_ENTRY_FAIL, LED_CMD_ON);
    g_wapi_config_state = CONFIG_STATE_FAILED;
    // g_wapi_config_state = CONFIG_STATE_NOT_CALLED;
    // g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
    osal_task_delay_ms(ERROR_PENDING_TIMEOUT_MS);
    // led_on_off(NETWORK_ENTRY_FAIL, LED_CMD_OFF);
    led_off_all();
    g_wapi_config_state = CONFIG_STATE_NOT_CALLED;
    g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
    low_power_schedule_sleep_ms(SLEEPTIME_COMPLETION_MS, true);
}

static void wapi_commu_conn(void)
{
    int32_t ret = m0804c_init(&g_wapi_handler_inst, (at_recv_callback_item_t){.pf_recv_parse = NULL, .arg = NULL});
    if (WAPI_OK != ret)
    {
        WAPI_COMMU_DEBUG_ERR("WAPI handler init failed: %d\r\n", ret);
        g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
        return;
    }

    ret = m0804c_use_cert_conn(&g_wapi_handler_inst);
    if (WAPI_OK != ret)
    {
        WAPI_COMMU_DEBUG_ERR("WAPI handler use cert connect failed: %d\r\n", ret);
        g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
        return;
    }
}

static void wapi_commu_fail_exec(void)
{
    wapi_close();
    led_on_off(NETWORK_ENTRY_FAIL, LED_CMD_ON);
    g_wapi_commu_state = COMMU_STATE_FAILED;
    // g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
    osal_task_delay_ms(ERROR_PENDING_TIMEOUT_MS);
    // led_on_off(NETWORK_ENTRY_FAIL, LED_CMD_OFF);
    led_off_all();
    g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
    low_power_schedule_sleep_ms(SLEEPTIME_COMPLETION_MS, true);
}

static void wapi_commu_fail(void)
{
    static uint8_t fail_count = 0;
    fail_count++;
    if(fail_count >= WAPI_PROCESS_FAIL_MAX)
    {
        fail_count = 0;
        wapi_commu_fail_exec();
    }
    else
    {
        wapi_commu_conn();
    }
}

#if 1
/* Queue carrying hall2 edge events from hall2_edge_event_cb() to wapi_commu_task. */
static osal_queue_handle_t g_hall2_edge_queue = NULL;

static osal_task_handle_t g_wapi_commu_task_handle = NULL;

m0804c_handler_t g_wapi_handler_inst = {0};

uart_rx_os_interface_t g_uart_os_interface =
{
    .pf_os_thread_create  = osal_task_create,
    .pf_os_thread_delete  = osal_task_delete,
    .pf_os_queue_create   = osal_queue_create,
    .pf_os_queue_put      = osal_queue_send,
    .pf_os_queue_get      = osal_queue_receive,
    .pf_os_enter_critical = osal_enter_critical,
    .pf_os_exit_critical  = osal_exit_critical,
};

/* AT handler OSAL table */
at_os_interface_t g_at_os_interface =
{
    .pf_sema_binary_create    = osal_sema_binary_create,
    .pf_sema_delete           = osal_sema_delete,
    .pf_sema_give             = osal_sema_give,
    .pf_sema_take             = osal_sema_take,
    .pf_timer_create          = osal_timer_create,
    .pf_timer_start           = osal_timer_start,
    .pf_timer_stop            = osal_timer_stop,
    .pf_timer_delete          = osal_timer_delete,
};

static m0804c_os_interface_t g_wapi_os_interface = {
    .pf_os_delay_ms = osal_task_delay_ms,
};

static frame_parse_att_t wapi_frame_parse_att =
{
    .recv_buf_att = &g_wapi_uart_rx_buf,
    .parse_algo = NULL,    /**< Use built-in parse algorithm */
};

static rx_thread_att_t wapi_rx_thread_att =
{
    .parse_thread_att =
    {
        .stack_depth = WAPI_COMMU_PARSE_THREAD_STACK_DEPTH,
        .thread_priority = WAPI_COMMU_PARSE_THREAD_PRIORITY - 1
    }
};

static uart_proto_input_arg_t wapi_uart_proto_input_arg =
{
    .frame_parse_att = &wapi_frame_parse_att,
    .uart_ops = &g_wapi_uart_ops,
    .os_interface = &g_uart_os_interface,
    .thread_att = &wapi_rx_thread_att
};

static at_input_arg_t  wapi_at_input_arg =
{
    .uart_proto_input_arg = &wapi_uart_proto_input_arg,
    .at_cmd_set_table = NULL,     /* Use built-in AT command table */
    .at_os_interface = &g_at_os_interface
};

static m0804c_pwr_ops_t wapi_pwr_ops =
{
    .pf_m0804c_open = m0804c_open,
    .pf_m0804c_close = m0804c_close,
};

static wapi_data_provider_t wapi_data_provider =
{
    .pf_get_cert_file = m0804c_get_cert_file,
    .pf_get_wapi_info = m0804c_get_wapi_info,
};

static wapi_data_provider_t wapi_cfg_data_provider =
{
    .pf_get_cert_file = m0804c_get_cert_file,
    .pf_get_wapi_info = m0804c_get_config_wapi_info,
};

static wapi_callback_t wapi_callbacks =
{
    .pf_process_success_cb = wapi_process_success_cb,
    .pf_process_err_cb = wapi_process_err_cb,
};

static wapi_callback_t wapi_cfg_callbacks =
{
    .pf_process_success_cb = wapi_process_cfg_success_cb,
    .pf_process_err_cb = wapi_process_cfg_err_cb,
};

static wapi_m0804c_input_arg_t wapi_input_arg =
{
    .at_input_arg = &wapi_at_input_arg,
    .os_interface = &g_wapi_os_interface,
    .pwr_ops = &wapi_pwr_ops,
    .data_provider = &wapi_data_provider,
    .callbacks = &wapi_callbacks
};

wapi_m0804c_input_arg_t wapi_cfg_input_arg =
{
    .at_input_arg = &wapi_at_input_arg,
    .os_interface = &g_wapi_os_interface,
    .pwr_ops = &wapi_pwr_ops,
    .data_provider = &wapi_cfg_data_provider,
    .callbacks = &wapi_cfg_callbacks
};
#endif

static int8_t wapi_send_data(const uint8_t *data, uint16_t len)
{
    int8_t ret = m0804c_send_blocking(&g_wapi_handler_inst, (uint8_t *)data, (uint16_t)len, NULL, OS_DELAY_MAX);
    WAPI_COMMU_DEBUG_OUT("WAPI hall2 event sent to M0804C, ret=%d", ret);
    return ret;
}

static int8_t get_unique_id_6bytes(uint8_t *uid)
{
    bsp_get_uid(uid);
    return 0;
}

static int8_t get_rfid_6bytes(uint8_t *rfid)
{
    /* Retrieve the last successfully read RFID tag from the global variable. */
    memcpy(rfid, g_last_rfid_tag, 6);
    return 0;
}


static void wapi_commu_task(void *argument)
{
    hall2_edge_event_t ev;

    while (1)
    {
        /* Block until an edge event is posted. */
        WAPI_COMMU_DEBUG_OUT("WAPI waiting for hall2 edge event...\r\n");
        int32_t ret = osal_queue_receive(g_hall2_edge_queue, &ev, OSAL_MAX_DELAY);
        if (ret != OSAL_SUCCESS)
        {
            WAPI_COMMU_DEBUG_OUT("WAPI hall2 event receive failed: %d\r\n", (int)ret);
            continue;
        }
        WAPI_COMMU_DEBUG_OUT("WAPI hall2 event received: edge=%d, tick=%u\r\n", ev.edge, ev.tick);

        // const char *edge_str = (ev.edge == SWITCH_EDGE_RISING) ? "rising" : "falling";

        // char pkt[64];
        // int pkt_len = snprintf(pkt, sizeof(pkt), "edge:%s rfid:%02X%02X%02X%02X%02X\r\n",
        //                        edge_str,
        //                        ev.rfid[0], ev.rfid[1], ev.rfid[2], ev.rfid[3], ev.rfid[4]);

        // if (pkt_len > 0)
        // {
        //     ret = m0804c_send_blocking(&g_wapi_handler_inst, (uint8_t *)pkt, (uint16_t)pkt_len, NULL, OS_DELAY_MAX);
        //     WAPI_COMMU_DEBUG_OUT("WAPI hall2 event sent to M0804C: %s, ret=%d", pkt, ret);
        // }
        memcpy(g_last_rfid_tag, ev.rfid, 5);  /* Store the last successfully read RFID tag in the global variable. */
        send_info_high_reliable_uplink(&g_high_relia_handler_inst, (ev.edge == SWITCH_EDGE_FALLING) ? H_MOUNT : H_UNMOUNT);
    }
}

static void protocol_init(void)
{
    high_relia_inst(&g_high_relia_handler_inst, &g_high_relia_ops);
}

void wapi_commu_init(void)
{
    g_wapi_commu_state = COMMU_STATE_RUNNING;
    wapi_status_t ret = WAPI_OK;
    if (g_hall2_edge_queue == NULL)
    {
        int32_t qret = osal_queue_create(SWITCH_HANDLER_QUEUE_DEPTH, sizeof(hall2_edge_event_t),
                                         &g_hall2_edge_queue);
        if (qret != OSAL_SUCCESS)
        {
            WAPI_COMMU_DEBUG_ERR("WAPI hall2 edge queue create failed: %d\r\n", (int)qret);
        }
        protocol_init();
    }
    if(g_wapi_commu_task_handle == NULL)
    {
        int32_t task_ret = osal_task_create("wapi_commu", wapi_commu_task, 1024, 20,
                                            &g_wapi_commu_task_handle, NULL);
        if (task_ret != OSAL_SUCCESS)
        {
            WAPI_COMMU_DEBUG_ERR("WAPI commu task create failed: %d\r\n", (int)task_ret);
        }
    }
    ret = m0804c_inst(&g_wapi_handler_inst, &wapi_input_arg);

    if(WAPI_ERR_ALREADY_INITED != ret && WAPI_OK != ret)
    {
        WAPI_COMMU_DEBUG_ERR("WAPI handler instance failed: %d\r\n", ret);
        g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
        return;
    }
    if(WAPI_ERR_ALREADY_INITED == ret)
    {
        m0804c_set_input_args(&g_wapi_handler_inst, &wapi_input_arg);
    }

    WAPI_COMMU_DEBUG_OUT("WAPI handler instance success\r\n");

    m0804c_open(&g_wapi_handler_inst);
    ret = m0804c_cert_upload(&g_wapi_handler_inst);
    if (WAPI_OK != ret)
    {
        WAPI_COMMU_DEBUG_ERR("WAPI handler cert upload failed: %d\r\n", ret);
        wapi_commu_fail_exec();
        return;
    }

    wapi_commu_conn();
    WAPI_COMMU_DEBUG_OUT("WAPI handler use cert connect success\r\n");
}

static wapi_status_t post_hall2_event(const hall2_edge_event_t *ev)
{
    if (g_hall2_edge_queue == NULL)
    {
        return WAPI_ERR_OTHERS;
    }

    /* No-wait send: never stall the producer (switch_handler / rfid thread). */
    int32_t ret = osal_queue_send(g_hall2_edge_queue, ev, 0U);
    if (ret != OSAL_SUCCESS)
    {
        WAPI_COMMU_DEBUG_ERR("WAPI hall2 event post dropped\r\n");
        return WAPI_ERR_OTHERS;
    }
    WAPI_COMMU_DEBUG_OUT("WAPI hall2 event posted: edge=%d, tick=%u\r\n", ev->edge, ev->tick);
    return WAPI_OK;
}

wapi_status_t wapi_commu_post_hall2_edge_rfid(switch_edge_t edge, const uint8_t *rfid)
{
    hall2_edge_event_t ev =
    {
        .edge = edge,
        .tick = bsp_get_tick(),
    };
    if (rfid != NULL)
    {
        memcpy(ev.rfid, rfid, RFID_DATA_LEN);
    }
    else
    {
        memset(ev.rfid, 0xFF, RFID_DATA_LEN);
    }
    return post_hall2_event(&ev);
}

void wapi_config_init(pf_at_recv_parse_t recv_parse_cb)
{
    g_wapi_config_state = CONFIG_STATE_CONFIGURING;
    wapi_status_t ret = WAPI_OK;

    ret = m0804c_inst(&g_wapi_handler_inst, &wapi_cfg_input_arg);

    if(WAPI_ERR_ALREADY_INITED != ret && WAPI_OK != ret)
    {
        g_wapi_config_state = CONFIG_STATE_NOT_CALLED;
        WAPI_COMMU_DEBUG_ERR("WAPI handler instance failed: %d\r\n", ret);
        return;
    }
    if(WAPI_ERR_ALREADY_INITED == ret)
    {
        m0804c_set_input_args(&g_wapi_handler_inst, &wapi_cfg_input_arg);
    }
    WAPI_COMMU_DEBUG_OUT("WAPI handler instance success\r\n");
    ret = m0804c_init(&g_wapi_handler_inst, (at_recv_callback_item_t){.pf_recv_parse = recv_parse_cb, .arg = NULL});
    if (WAPI_OK != ret)
    {
        g_wapi_config_state = CONFIG_STATE_NOT_CALLED;
        WAPI_COMMU_DEBUG_ERR("WAPI handler init failed: %d\r\n", ret);
        return;
    }

    ret = m0804c_use_pwd_conn(&g_wapi_handler_inst);
    if (WAPI_OK != ret)
    {
        g_wapi_config_state = CONFIG_STATE_NOT_CALLED;
        WAPI_COMMU_DEBUG_ERR("WAPI handler use pwd connect failed: %d\r\n", ret);
        return;
    }
    WAPI_COMMU_DEBUG_OUT("WAPI handler use pwd connect success\r\n");
    // m0804c_recv_parse(&g_wapi_handler_inst, recv_parse_cb);
}

wapi_config_state_t wapi_query_config_state(void)
{
    return g_wapi_config_state;
}

void wapi_set_config_state(wapi_config_state_t state)
{
    g_wapi_config_state = state;
}

#if 1
static void m0804c_open(struct m0804c_handler *const self)
{
    bsp_wapi_wake_low();
    bsp_wapi_pwr_on();
    osal_task_delay_ms(2000);
    WAPI_COMMU_DEBUG_OUT("WAPI M0804C opened\r\n");
}
static void m0804c_close(struct m0804c_handler *const self)
{
    bsp_wapi_wake_high();
    bsp_wapi_pwr_off();
    osal_task_delay_ms(2000);
    WAPI_COMMU_DEBUG_OUT("WAPI M0804C closed\r\n");
}

static wapi_info_t *m0804c_get_wapi_info(struct m0804c_handler *const self)
{
    return get_wapi_info();
}

static wapi_info_t *m0804c_get_config_wapi_info(struct m0804c_handler *const self)
{
    return get_wapi_cfg_info();
}

static cert_file_t *m0804c_get_cert_file(struct m0804c_handler *const self)
{
    return get_cert_file();
}

static void wapi_process_success_cb(struct m0804c_handler *const self, wapi_process_type_t process_type)
{
    switch (process_type)
    {
        case PROCESS_INIT:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS INIT SUCCESS\r\n");
            break;
        case PROCESS_CERT_AUTH:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CERT AUTH SUCCESS\r\n");
            break;
        case PROCESS_PWD_AUTH:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS PWD AUTH SUCCESS\r\n");
            break;
        case PROCESS_CONNECT:
            led_on_off(WAPI_COMMU, LED_CMD_ON);
            g_wapi_commu_state = COMMU_STATE_SUCCESS;
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CONNECT SUCCESS\r\n");
            break;
        default:
            break;
    }
}

static void wapi_process_err_cb(struct m0804c_handler *const self, wapi_process_type_t process_type)
{
    switch (process_type)
    {
        case PROCESS_INIT:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS INIT ERROR\r\n");
            wapi_commu_fail();
            break;
        case PROCESS_CERT_AUTH:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CERT AUTH ERROR\r\n");
            wapi_commu_fail();
            break;
        case PROCESS_PWD_AUTH:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS PWD AUTH ERROR\r\n");
            wapi_commu_fail();
            break;
        case PROCESS_CONNECT:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CONNECT ERROR\r\n");
            wapi_commu_fail();
            break;
        default:
            break;
    }
}

static void wapi_process_cfg_success_cb(struct m0804c_handler *const self, wapi_process_type_t process_type)
{
    switch (process_type)
    {
        case PROCESS_INIT:
            // led_off_all();
            // led_on_off(WAPI_CONFIG_CONNING, LED_CMD_ON);
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS INIT SUCCESS\r\n");
            break;
        case PROCESS_CERT_AUTH:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CERT AUTH SUCCESS\r\n");
            break;
        case PROCESS_PWD_AUTH:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS PWD AUTH SUCCESS\r\n");
            break;
        case PROCESS_CONNECT:
            led_on_off(WAPI_CONFIG, LED_CMD_ON);
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CONNECT SUCCESS\r\n");
            g_wapi_config_state = CONFIG_STATE_SUCCESS;
            break;
        default:
            break;
    }
}

static void wapi_process_cfg_err_cb(struct m0804c_handler *const self, wapi_process_type_t process_type)
{
    switch (process_type)
    {
        case PROCESS_INIT:
            wapi_config_fail();
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS INIT ERROR\r\n");
            break;
        case PROCESS_CERT_AUTH:
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CERT AUTH ERROR\r\n");
            break;
        case PROCESS_PWD_AUTH:
            wapi_config_fail();
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS PWD AUTH ERROR\r\n");
            break;
        case PROCESS_CONNECT:
            wapi_config_fail();
            WAPI_COMMU_DEBUG_OUT("WAPI PROCESS CONNECT ERROR\r\n");
            break;
        default:
            break;
    }
}
#endif

wapi_status_t wapi_send(uint8_t *buf, uint16_t length)
{
    return m0804c_send(&g_wapi_handler_inst, buf, length, NULL);
}

void wapi_close(void)
{
    m0804c_close(&g_wapi_handler_inst);
}

/* -------------------------------------------------------------------------- */
/*                        Low-power enter / exit                              */
/* -------------------------------------------------------------------------- */

void wapi_commu_enter_low_power(void)
{
    /* Put the M0804C into its low-power state, then set all WAPI pins to analog
     * input mode to minimise sleep current. */
    g_wapi_config_state = CONFIG_STATE_NOT_CALLED;
    g_wapi_commu_state = COMMU_STATE_NOT_CALLED;
    bsp_wapi_enter_low_power();
}

void wapi_commu_exit_low_power(void)
{
    /* Restore the WAPI pins to their functional configuration. */
    bsp_wapi_exit_low_power();
}
