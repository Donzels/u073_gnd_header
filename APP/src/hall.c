#include "hall.h"
#include "switch_handler.h"
#include "stm32u0xx_hal.h"
#include "dbg_console.h"
#include "upper_commu.h"
#include "led.h"
#include "wapi_commu.h"
#include "rfid.h"
#include "low_power.h"
#include "main.h"
#include "voltage.h"

#define HALL_DEBUG_OUT(fmt, ...)    dbg_console_printf(fmt "\r\n", ##__VA_ARGS__)

switch_handler_t g_hall_handler_inst;
switch_handler_t g_hall2_handler_inst;

/* -------------------------------------------------------------------------- */
/*                        OS-abstraction interface                            */
/* -------------------------------------------------------------------------- */

static SWITCH_TICK_TYPE hall_get_tick_wrapper(void)
{
    return (SWITCH_TICK_TYPE)HAL_GetTick();
}

/* HAL_GetTick() is 1 kHz: 1 tick == 1 ms. */
static uint32_t hall_tick_2_ms_wrapper(SWITCH_TICK_TYPE tick)
{
    return (uint32_t)tick;
}

static switch_os_interface_t g_switch_os_interface =
{
    .pf_get_tick       = hall_get_tick_wrapper,
    .pf_tick_2_ms      = hall_tick_2_ms_wrapper,
    .pf_queue_create   = (int32_t (*)(size_t, size_t, void **))osal_queue_create,
    .pf_queue_delete   = (void (*)(void *))osal_queue_delete,
    .pf_queue_send     = (int32_t (*)(void *, const void *, uint32_t))osal_queue_send,
    .pf_queue_receive  = (int32_t (*)(void *, void *, uint32_t))osal_queue_receive,
    .pf_thread_create  = (int32_t (*)(const char *, void (*)(void *), size_t, uint32_t, void **, void *))osal_task_create,
    .pf_delay_ms       = osal_task_delay_ms,
};

static void under_voltage_callback(void)
{
    low_power_schedule_sleep_ms(UNDERVOLTAGE_SLEEP_MS, true); 
    led_on_off(UNDER_VOLTAGE, LED_CMD_ON);       
}

/* -------------------------------------------------------------------------- */
/*                        Hall1 (HALL_EXTI) PRESS callback                    */
/* -------------------------------------------------------------------------- */

void hall_press_event_cb(switch_press_type_t press_type)
{
    if(voltage_is_low())
    {
        HALL_DEBUG_OUT("HALL1: edge ignored due to low voltage");
        under_voltage_callback(); 
        return;
    }
    switch (press_type)
    {
    case SWITCH_PRESS_SHORT:
        HALL_DEBUG_OUT("HALL: short press");
        /* TODO: application short-press action */
        break;

    case SWITCH_PRESS_LONG:
        HALL_DEBUG_OUT("HALL: long press");

        if(COMMU_STATE_RUNNING != wapi_query_commu_state() && COMMU_STATE_FAILED != wapi_query_commu_state())
        {
            if(wapi_query_config_state() == CONFIG_STATE_NOT_CALLED)
            {
                low_power_schedule_sleep_ms(SLEEPTIME_CONFIG_MS, true);
                led_off_all();
                led_on_off(WAPI_CONFIG_CONNING, LED_CMD_ON);
                wapi_config();                
            }
            else if(wapi_query_config_state() == CONFIG_STATE_SUCCESS)
            {
                low_power_schedule_sleep_ms(SLEEPTIME_COMPLETION_MS, true);
                led_off_all();
                wapi_close();
                wapi_set_config_state(CONFIG_STATE_NOT_CALLED);
                wapi_set_commu_state(COMMU_STATE_NOT_CALLED);                
            }
        }        
        /* wapi will close when CONFIG_STATE_FAILED in callback */
  
        /* TODO: application long-press action */
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/*                        Hall1 (HALL_EXTI) EDGE callback                     */
/* -------------------------------------------------------------------------- */

static void hall1_edge_event_cb(switch_edge_t edge)
{
    HALL_DEBUG_OUT("HALL1: edge detected, is %d", edge);
    if(voltage_is_low())
    {
        HALL_DEBUG_OUT("HALL1: edge ignored due to low voltage");
        under_voltage_callback(); 
        return;
    }
    /* TODO: application hall1 edge action */
    low_power_schedule_sleep_ms(SLEEPTIME_COMPLETION_MS, true);
}

/* -------------------------------------------------------------------------- */
/*                        Hall2 (WKUP_EXTI) EDGE callback                     */
/* -------------------------------------------------------------------------- */

static void hall2_edge_event_cb(switch_edge_t edge)
{
    if(voltage_is_low())
    {
        HALL_DEBUG_OUT("HALL2: edge ignored due to low voltage");
        under_voltage_callback(); 
        return;
    }
    if (edge == SWITCH_EDGE_RISING || edge == SWITCH_EDGE_FALLING)
    {
        HALL_DEBUG_OUT("HALL2: edge detected, is %d", edge);
        // if(COMMU_STATE_NOT_CALLED == wapi_query_commu_state() && COMMU_STATE_SUCCESS == wapi_query_commu_state())
        {
            if(wapi_query_config_state() == CONFIG_STATE_NOT_CALLED)
            {            
                if(wapi_query_commu_state() == COMMU_STATE_NOT_CALLED)
                {
                    low_power_schedule_sleep_ms(SLEEPTIME_COMMU_MS, true);
                    led_off_all();
                    led_on_off(WAPI_COMMU_CONNING, LED_CMD_ON);
                    wapi_commu_init();
                    rfid_start_with_retry(edge);                
                }    
                // if(wapi_query_commu_state() == COMMU_STATE_SUCCESS)  
                else if(wapi_query_commu_state() != COMMU_STATE_FAILED)
                {
                    /* Start RFID read with retry logic. */
                    low_power_schedule_sleep_ms(SLEEPTIME_COMMU_MS, true);
                    rfid_start_with_retry(edge);                 
                }         
            }  
        }                
    }
}

/* -------------------------------------------------------------------------- */
/*                        Initialisation                                       */
/* -------------------------------------------------------------------------- */

void hall_enter_low_power(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(HALL_EXTI_Pin | WKUP_EXTI_Pin);
}

void hall_exit_low_power(void)
{

}

void hall2_sample_edge(void)
{
    GPIO_PinState level = HAL_GPIO_ReadPin(WKUP_EXTI_GPIO_Port, WKUP_EXTI_Pin);

    if (level == GPIO_PIN_RESET)   /* low → falling edge */
    {
        switch_handler_falling_callback(&g_hall2_handler_inst);
    }
    else                            /* high → rising edge */
    {
        switch_handler_rising_callback(&g_hall2_handler_inst);
    }
}

void hall_init(void)
{
    switch_handler_status_t status;
    switch_listener_t *handle;

    /* Hall1: HALL_EXTI, PRESS mode, press begins on falling edge. */
    status = switch_inst(&g_hall_handler_inst, &g_switch_os_interface);
    if (status != SWITCH_HANDLER_OK)
    {
        HALL_DEBUG_OUT("HALL: inst failed, status=%d", (int)status);
    }
    else
    {       
        /* Hall1: also register an EDGE listener for per-edge notifications. */
        switch_arg_u hall1_edge_arg =
        {
            .edge =
            {
                .edge_cb     = hall1_edge_event_cb,
                .debounce_ms = 30U,
            },
        };
        status = switch_register(&g_hall_handler_inst, SWITCH_CB_EDGE, &hall1_edge_arg, &handle);
        HALL_DEBUG_OUT("HALL: edge register %s", (status == SWITCH_HANDLER_OK) ? "ok" : "failed");

        switch_arg_u hall1_arg =
        {
            .press =
            {
                .press_cb             = hall_press_event_cb,
                .start_edge           = SWITCH_EDGE_FALLING,
                .debounce_ms          = 30U,
                .short_press_max_ms   = 1000U,
                .long_press_min_ms    = 2000U,
            },
        };
        status = switch_register(&g_hall_handler_inst, SWITCH_CB_PRESS, &hall1_arg, &handle);
        HALL_DEBUG_OUT("HALL: inst + register %s", (status == SWITCH_HANDLER_OK) ? "ok" : "failed");
    }

    /* Hall2: WKUP_EXTI, EDGE mode — reports every debounced edge. */
    status = switch_inst(&g_hall2_handler_inst, &g_switch_os_interface);
    if (status != SWITCH_HANDLER_OK)
    {
        HALL_DEBUG_OUT("HALL2: inst failed, status=%d", (int)status);
    }
    else
    {
        switch_arg_u hall2_arg =
        {
            .edge =
            {
                .edge_cb     = hall2_edge_event_cb,
                .debounce_ms = 5U,
            },
        };
        status = switch_register(&g_hall2_handler_inst, SWITCH_CB_EDGE, &hall2_arg, &handle);
        HALL_DEBUG_OUT("HALL2: inst + register %s", (status == SWITCH_HANDLER_OK) ? "ok" : "failed");
    }
    /* first time initialization: trigger a hall2 falling edge to kick off the flow.
     * Use switch_handler_falling_callback() so the edge is unambiguously FALLING
     * (SWIER would set both rising + falling pending and fire both callbacks). */
    hall2_sample_edge();
}
