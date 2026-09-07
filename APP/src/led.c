#include "led.h"
#include "bsp.h"
#include "osal.h"

#define LED_FLASH_PERIOD_TICK_FAST  25
#define LED_FLASH_PERIOD_TICK_SLOW  200
#define LED_FLASH_DUTY              20
#define LED_FULL_DUTY               100

static void led_red_on(void);
static void led_green_on(void);
static void led_blue_on(void);
static void led_yellow_on(void);
static void led_magenta_on(void);
static void led_cyan_on(void);
static void led_off(void);

static led_os_interface_t led_os_interface =
{
    .pf_os_thread_create   = osal_task_create,
    .pf_os_thread_delete   = osal_task_delete,
    .pf_os_delay           = osal_task_delay,
#ifdef LED_HANDLER_USE_QUEUE_MODE
    /* Queue mode: handler thread owns led_status[] exclusively, so the queue API
     * replaces the critical-section API. */
    .pf_os_queue_create    = (int32_t (*)(size_t, size_t, void **))osal_queue_create,
    .pf_os_queue_delete    = (void (*)(void *))osal_queue_delete,
    .pf_os_queue_send      = (int32_t (*)(void *, const void *, uint32_t))osal_queue_send,
    .pf_os_queue_receive   = (int32_t (*)(void *, void *, uint32_t))osal_queue_receive,
#else
    .pf_os_enter_critical  = osal_enter_critical,
    .pf_os_exit_critical   = osal_exit_critical,
#endif /* LED_HANDLER_USE_QUEUE_MODE */
#ifdef LED_HANDLER_USE_SEMA_BLOCK
    .pf_os_sema_binary_create = osal_sema_binary_create,
    .pf_os_sema_delete       = osal_sema_delete,
    .pf_os_sema_give         = osal_sema_give,
    .pf_os_sema_take         = osal_sema_take
#endif
};

static led_control_t led_control_table[] =
{
    {UNDER_VOLTAGE, 1, {{led_yellow_on, led_off}}, LED_FLASH_DUTY, LED_FLASH_PERIOD_TICK_FAST, FLASH_CNT_MAX},
    {NETWORK_ENTRY_FAIL, 1, {{led_red_on, led_off}}, LED_FLASH_DUTY, LED_FLASH_PERIOD_TICK_FAST, FLASH_CNT_MAX},

    {WAPI_COMMU, 1, {{led_green_on, led_off}}, LED_FLASH_DUTY, LED_FLASH_PERIOD_TICK_SLOW, FLASH_CNT_MAX},
    {WAPI_COMMU_CONNING, 1, {{led_green_on, led_off}}, LED_FLASH_DUTY, LED_FLASH_PERIOD_TICK_FAST, FLASH_CNT_MAX},

    {WAPI_CONFIG, 1, {{led_blue_on, led_off}}, LED_FLASH_DUTY, LED_FLASH_PERIOD_TICK_SLOW, FLASH_CNT_MAX},
    {WAPI_CONFIG_CONNING, 1, {{led_blue_on, led_off}}, LED_FLASH_DUTY, LED_FLASH_PERIOD_TICK_FAST, FLASH_CNT_MAX},

};

static led_input_arg_t led_input_args =
{
    .table = led_control_table,
    .table_num = sizeof(led_control_table) / sizeof(led_control_table[0]),
    .os_if = &led_os_interface
};

led_handler_t   g_led_handler;

void led_init(void)
{
    led_handler_status_t ret = led_handler_inst(&g_led_handler, &led_input_args);
    if(ret == LED_HANDLER_OK)
    {
        LED_DEBUG_OUT("LED handler instantiated successfully.");
    }
    else
    {
        LED_DEBUG_OUT("LED handler instantiation failed with error: %d", ret);
        return;
    }

    // led_ctrl(&g_led_handler, MOUNT, LED_CMD_ON);
}

void led_on_off(uint8_t led_lable, led_cmd_t led_cmd)
{
    led_handler_status_t ret = led_ctrl(&g_led_handler, led_lable, led_cmd);
    if(ret != LED_HANDLER_OK)
    {
        LED_DEBUG_OUT("LED control failed for label %u with command %u. Error: %d", led_lable, led_cmd, ret);
    }
}

void led_off_all(void)
{
    led_handler_status_t ret = led_all_off(&g_led_handler);
    if(ret != LED_HANDLER_OK)
    {
        LED_DEBUG_OUT("LED all off failed. Error: %d", ret);
    }
}

void led_enter_low_power(void)
{
    /* Turn off all LEDs, then set LED pins to analog to eliminate GPIO leakage. */
    led_off_all();
    bsp_led_enter_low_power();
}

void led_exit_low_power(void)
{
    /* Restore LED pins to open-drain output, all off. */
    bsp_led_exit_low_power();
}

#if 1
static void led_red_on(void)
{
    bsp_led_red_on();
}
static void led_green_on(void)
{
    bsp_led_green_on();
}
static void led_blue_on(void)
{
    bsp_led_blue_on();
}
static void led_yellow_on(void)
{
    bsp_led_yellow_on();
}
static void led_magenta_on(void)
{
    bsp_led_magenta_on();
}
static void led_cyan_on(void)
{
    bsp_led_cyan_on();
}
static void led_off(void)
{
    bsp_led_all_off();
}
#endif
