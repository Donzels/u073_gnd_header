/**
 * @file rfid.c
 * @brief RFID application layer — hardware ops + OS-interface binding.
 *
 * Ported from the STM32L010 (TIM2-only) implementation. The input-capture
 * timer is TIM15_CH1/CH2. The 125 kHz carrier PWM stays on TIM2_CH3.
 *
 * TIM15 is configured (CubeMX) in PWM-input style: CH1 captures the RISING
 * edge (DIRECTTI), CH2 captures the FALLING edge (INDIRECTTI), free-running
 * 1 us tick, ARR = 65535. Edge detection reads the CC1/CC2 capture flags
 * rather than the pin level, so it is reliable in time-sensitive ISR context.
 */

#include "rfid.h"
#include "main.h"
#include "osal.h"
#include "dbg_console.h"
#include "wapi_commu.h"

#define RF_DEBUG_OUT(fmt, ...)    dbg_console_printf(fmt, ##__VA_ARGS__)

/* Maximum number of read attempts before giving up (first try + retries). */
#define RFID_READ_MAX_ATTEMPTS    3

/* Defined in main.c (CubeMX-generated); not declared extern in any header. */
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim15;

static rfid_handler_t g_rfid_handler = {0};

/* -------------------------------------------------------------------------- */
/*                        OS-abstraction interface                            */
/* -------------------------------------------------------------------------- */

static rfid_os_interface_t g_rfid_os_interface =
{
    .pf_os_thread_create = (int32_t (*)(const char *, void (*)(void *), size_t,
                                        uint32_t, void **, void *))osal_task_create,
    .pf_os_sema_create   = (int32_t (*)(void **, uint32_t, uint32_t))osal_sema_countings_create,
    .pf_os_sema_delete   = (void (*)(void *))osal_sema_delete,
    .pf_os_sema_give     = (int32_t (*)(void *))osal_sema_give,
    .pf_os_sema_take     = (int32_t (*)(void *, uint32_t))osal_sema_take,
    .pf_os_queue_create  = (int32_t (*)(size_t, size_t, void **))osal_queue_create,
    .pf_os_queue_delete  = (void (*)(void *))osal_queue_delete,
    .pf_os_queue_send    = (int32_t (*)(void *, const void *, uint32_t))osal_queue_send,
    .pf_os_queue_receive = (int32_t (*)(void *, void *, uint32_t))osal_queue_receive,
};

/* -------------------------------------------------------------------------- */
/*                        Hardware operations                                  */
/* -------------------------------------------------------------------------- */

static void __rfid_init(void)
{
    /* TIM15 / TIM2 / GPIO are configured by CubeMX (MX_xxx_Init). */
}

static void rfid_open(void)
{
    HAL_GPIO_WritePin(GPIOA, RFID_PWR_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, RFID_SLEEP_Pin, GPIO_PIN_SET);
    osal_task_delay_ms(10);
    HAL_GPIO_WritePin(GPIOA, PWM_N_Pin, GPIO_PIN_SET);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    HAL_TIM_IC_Start_IT(&htim15, TIM_CHANNEL_1);
    HAL_TIM_IC_Start_IT(&htim15, TIM_CHANNEL_2);
}

static void rfid_close(void)
{
    HAL_GPIO_WritePin(GPIOA, RFID_PWR_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, RFID_SLEEP_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, PWM_N_Pin, GPIO_PIN_RESET);
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
    HAL_TIM_IC_Stop_IT(&htim15, TIM_CHANNEL_1);
    HAL_TIM_IC_Stop_IT(&htim15, TIM_CHANNEL_2);
}

/* -------------------------------------------------------------------------- */
/*                        Input-capture operations                             */
/* -------------------------------------------------------------------------- */

static capture_edge_t get_capture_edge(void)
{
    /* Read the capture flags directly: CC1 = rising (DIRECTTI),
     * CC2 = falling (INDIRECTTI). Reliable in time-sensitive ISR context,
     * unlike sampling the pin level. */
    if (__HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC1))
    {
        // RF_DEBUG_OUT("r");
        return EDGE_RISING;
    }
    if (__HAL_TIM_GET_FLAG(&htim15, TIM_FLAG_CC2))
    {
        // RF_DEBUG_OUT("f");
        return EDGE_FALLING;
    }
    // RF_DEBUG_OUT("IC: no edge");
    return EDGE_ERR; /* fallback */
}

static uint32_t get_ic_value(capture_edge_t capture_edge)
{
    uint32_t cur_channel = (capture_edge == EDGE_RISING) ? TIM_CHANNEL_1 : TIM_CHANNEL_2;
    return HAL_TIM_ReadCapturedValue(&htim15, cur_channel);
}

static uint32_t get_arr(void)
{
    return __HAL_TIM_GET_AUTORELOAD(&htim15);
}

/* -------------------------------------------------------------------------- */
/*                        Decode-complete callback                            */
/* -------------------------------------------------------------------------- */

void rfid_cpl_callback(uint8_t *rfid, void *arg)
{
    (void)arg;
    RF_DEBUG_OUT("RFID: tag decoded [%02X %02X %02X %02X %02X]\r\n",
                   (unsigned)rfid[0], (unsigned)rfid[1], (unsigned)rfid[2],
                   (unsigned)rfid[3], (unsigned)rfid[4]);
    /* TODO: application action on decoded tag */
}

/* -------------------------------------------------------------------------- */
/*                    Async read + edge event post to wapi_commu_task           */
/* -------------------------------------------------------------------------- */

/*
 * Retry budget for a hall2-triggered read. Passed straight to rfid_read_asy(),
 * which performs up to this many capture attempts internally.
 */
#define RFID_READ_MAX_ATTEMPTS    3

/* Queue carrying the edge that triggered each RFID read. rfid_start_with_retry
 * pushes the edge; rfid_read_cpl_cb pops it so the edge survives even if a new
 * edge arrives before the current read completes.
 * Element type is uint32_t so every slot is 4-byte aligned — required by the
 * STM32U073 (Cortex-M0+) queue otherwise a hard fault can occur. */
static osal_queue_handle_t g_rfid_edge_queue = NULL;

/* Completion callback installed on the handler. Called once, after rfid_read_asy
 * has exhausted its retry budget or decoded a tag. Pops the matching edge from
 * the queue and forwards edge + rfid together to wapi_commu_task. */
static void rfid_read_cpl_cb(uint8_t *rfid, void *arg)
{
    (void)arg;

    uint32_t edge_raw = SWITCH_EDGE_RISING;  /* fallback, should not happen in normal flow */

    /* Retrieve the edge that triggered this read. */
    if (g_rfid_edge_queue != NULL)
    {
        osal_queue_receive(g_rfid_edge_queue, &edge_raw, 0);
    }

    switch_edge_t edge = (switch_edge_t)edge_raw;

    RF_DEBUG_OUT("RFID: tag decoded [%02X %02X %02X %02X %02X]\r\n",
                 (unsigned)rfid[0], (unsigned)rfid[1], (unsigned)rfid[2],
                 (unsigned)rfid[3], (unsigned)rfid[4]);

    /* edge + rfid (all-0xFF if every attempt failed) -> wapi_commu_task. */
    wapi_commu_post_hall2_edge_rfid(edge, rfid);
}

static rfid_cpl_cb_ctx_t g_rfid_cpl_cb =
{
    .arg            = NULL,
    .pf_rfid_cpl_cb = rfid_read_cpl_cb
};

/* Start an async RFID read triggered by a hall2 edge. rfid_read_asy() retries
 * internally up to RFID_READ_MAX_ATTEMPTS, then this callback forwards the
 * final edge + rfid to wapi_commu_task. Safe to call from the switch handler
 * thread; each edge is queued so none are lost even under rapid triggering. */
void rfid_start_with_retry(switch_edge_t edge)
{
    /* Queue the edge so the completion callback can forward it with the tag. */
    if (g_rfid_edge_queue != NULL)
    {
        uint32_t edge_raw = (uint32_t)edge;
        osal_queue_send(g_rfid_edge_queue, &edge_raw, 0U);
        RF_DEBUG_OUT("RFID: edge queued for read: %d\r\n", edge);
    }

    rfid_handler_status_t status = rfid_read_asy(&g_rfid_handler, &g_rfid_cpl_cb,
                                                 RFID_READ_MAX_ATTEMPTS);
    if (status != RFID_HANDLER_OK)
    {
        RF_DEBUG_OUT("RFID: read start failed, status=%d\r\n", (int)status);
    }
}

/* -------------------------------------------------------------------------- */
/*                        Ops tables + input arg                               */
/* -------------------------------------------------------------------------- */

static rfid_ops_t g_rfid_ops =
{
    .pf_rfid_init  = __rfid_init,
    .pf_rfid_open  = rfid_open,
    .pf_rfid_close = rfid_close
};

/* NOTE: RFID_MORE_PRECISE = 1, so the handler uses the per-edge capture path
 * (pf_get_ic_value + pf_get_arr). The counter/clear_counter fields only exist
 * in the non-precise path and are intentionally not wired here. */
static io_capture_ops_t g_io_capture_ops =
{
    .pf_get_capture_edge = get_capture_edge,
    .pf_get_ic_value     = get_ic_value,
    .pf_get_arr          = get_arr
};

static rfid_handler_input_arg_t g_rfid_handler_input_arg =
{
    .rfid_ops       = &g_rfid_ops,
    .io_capture_ops = &g_io_capture_ops,
    .os_if          = &g_rfid_os_interface
};

/* -------------------------------------------------------------------------- */
/*                        Initialisation                                       */
/* -------------------------------------------------------------------------- */

void rfid_init(void)
{
    rfid_handler_status_t status = rfid_handler_inst(&g_rfid_handler,
                                                     &g_rfid_handler_input_arg);
    if (status != RFID_HANDLER_OK)
    {
        RF_DEBUG_OUT("RFID: init failed, status=%d", (int)status);
        return;
    }

    if (g_rfid_edge_queue == NULL)
    {
        int32_t qret = osal_queue_create(SWITCH_HANDLER_QUEUE_DEPTH, sizeof(uint32_t),
                                         &g_rfid_edge_queue);
        if (qret != OSAL_SUCCESS)
        {
            RF_DEBUG_OUT("RFID: edge queue create failed: %d\r\n", (int)qret);
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                        ISR callback                                         */
/* -------------------------------------------------------------------------- */

// void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
// {
//     if (htim->Instance == TIM15)
//     {
//         demo_algo_isr_cb(&g_rfid_handler);
//     }
// }

void ic_isr_cb(void)
{
    demo_algo_isr_cb(&g_rfid_handler);
}

/* Reconfigure all RFID pins to analog input mode to eliminate GPIO leakage.
 * Covers: PA2 (TIM2_CH3), PA3 (PWM_N), PA4 (RFID_PWR), PA5 (RFID_SLEEP),
 *         PB14 (TIM15_CH1). */
static void rfid_gpio_analog(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIOA: PA2/PA3/PA4/PA5 → analog, no pull */
    GPIO_InitStruct.Pin  = GPIO_PIN_2 | PWM_N_Pin | RFID_PWR_Pin | RFID_SLEEP_Pin;
    // GPIO_InitStruct.Pin  = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* GPIOB: PB14 (TIM15_CH1) → analog, no pull */
    GPIO_InitStruct.Pin = GPIO_PIN_14;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* Restore all RFID pins to their functional configuration:
 * PA2 → AF1 (TIM2_CH3), PA3/PA4/PA5 → push-pull output, PB15 → AF14 (TIM15_CH1). */
static void rfid_gpio_restore(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA2: TIM2_CH3, AF1, push-pull */
    GPIO_InitStruct.Pin       = GPIO_PIN_2;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA3/PA4/PA5: PWM_N / RFID_PWR / RFID_SLEEP, push-pull output */
    GPIO_InitStruct.Pin       = PWM_N_Pin | RFID_PWR_Pin | RFID_SLEEP_Pin;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0U;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB14: TIM15_CH1, AF14, push-pull */
    GPIO_InitStruct.Pin       = GPIO_PIN_14;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF14_TIM15;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void rfid_enter_low_power(void)
{
    /* Stop the carrier PWM and input-capture timer, then set all RFID pins to
     * analog input mode to minimise sleep current. */
    rfid_close();
    rfid_gpio_analog();
    // HAL_GPIO_WritePin(GPIOA, RFID_PWR_Pin, GPIO_PIN_SET);
    // HAL_GPIO_WritePin(GPIOA, PWM_N_Pin | RFID_SLEEP_Pin, GPIO_PIN_RESET);
}

void rfid_exit_low_power(void)
{
    /* Restore the RFID pins to their functional configuration. */
    rfid_gpio_restore();
}
