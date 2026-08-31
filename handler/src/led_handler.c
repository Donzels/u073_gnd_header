/**
 * File Name          : led_handler.c
 * Description        : LED Control Handler (FreeRTOS Implementation)
 * @author            : Donzel
 * @version           : 1.0.0
 * @date              : 2025-12-22
 *
 * LED state management with priority control and flash pattern scheduling
 * via RTOS tasks.
 */

/* Includes ------------------------------------------------------------------*/
#include "led_handler.h"

/* Memory Allocation Macros --------------------------------------------------*/
#include <stdlib.h>
#define MALLOC(size)        malloc(size)
#define FREE(ptr)           free(ptr)

/* Input Arguments Access Macros ---------------------------------------------*/
#define TABLE(self)         (self)->input_args->table       /* LED control table */
#define TABLE_NUM(self)     (self)->input_args->table_num   /* Number of table entries */
#define OS_IF(self)         (self)->input_args->os_if       /* OS interface */
#define PRIV_DATA(self)     (self)->priv_data               /* Private internal data */        

/* Private Status Structure --------------------------------------------------*/
/**
 * @brief LED private runtime status structure
 * Stores dynamic state for each LED label
 */
typedef struct
{
    led_cmd_t   led_cmd;              /* Current LED command (ON/OFF) */
    uint8_t     cur_level;            /* Current LED operation level (1~LED_OPS_LEVEL_MAX) */
    uint16_t    remain_flash_cnt;     /* Remaining flash cycles */
    uint16_t    remain_period_tick;   /* Remaining ticks in current flash period */
} led_pri_status_t;

#ifdef LED_HANDLER_USE_QUEUE_MODE
/* Queue command payload -----------------------------------------------------*/
/**
 * @brief Command posted to the handler thread's queue (queue mode only).
 *
 * led_ctrl() / led_all_off() never touch led_status[] directly in queue mode;
 * they post one of these instead. The handler thread — the sole owner of
 * led_status[] — applies each command when it drains the queue.
 */
typedef struct
{
    uint8_t     led_lable;  /* Target LED label */
    led_cmd_t   cmd;        /* Command (LED_CMD_ON / LED_CMD_OFF) */
    uint16_t    dummy; 
} led_queue_cmd_t;
#endif /* LED_HANDLER_USE_QUEUE_MODE */

/* Private Handler Context ---------------------------------------------------*/
/**
 * @brief LED handler private data structure
 * Stores initialization state, control table pointer, and LED status array
 */
typedef struct led_handler_priv_data
{
    bool                if_init;      /* Initialization flag (true = initialized) */
    led_pri_status_t    *led_status;  /* Array of LED runtime status (per label) */
    void                *thread_handle; /* OS thread handle for LED control task */
#ifdef LED_HANDLER_USE_SEMA_BLOCK
    void                *sema_handle; /* Event-gate semaphore (blocks thread when all LEDs off) */
#endif
#ifdef LED_HANDLER_USE_QUEUE_MODE
    void                *queue_handle; /* Command queue (led_ctrl / led_all_off -> handler thread) */
#endif
} led_handler_priv_data_t;

/* Critical-section helpers --------------------------------------------------*/
/* In queue mode the handler thread is the sole owner of led_status[], so no
 * critical section is needed. These macros compile the enter/exit calls only
 * in the original (shared-state) mode, leaving the queue-mode code empty. */
#ifdef LED_HANDLER_USE_QUEUE_MODE
  #define LED_CS_DECL(self)     ((void)0)
  #define LED_CS_ENTER(self)    ((void)0)
  #define LED_CS_EXIT(self)     ((void)0)
#else
  #define LED_CS_DECL(self)     uint32_t primask
  #define LED_CS_ENTER(self)    do { primask = OS_IF(self)->pf_os_enter_critical(); } while (0)
  #define LED_CS_EXIT(self)     do { OS_IF(self)->pf_os_exit_critical(primask); } while (0)
#endif

/* Private LED Control Task --------------------------------------------------*/
/**
 * @brief LED control task (FreeRTOS thread)
 * Manages LED flash timing, duty cycle, and state transitions
 * @param arg Pointer to LED control table
 */
#ifdef LED_HANDLER_USE_QUEUE_MODE
/* Apply one queued command to led_status[] (queue mode only).
 * The handler thread is the sole owner of led_status[], so this runs with no
 * critical section. ON resets the flash parameters exactly like the original
 * led_ctrl() ON path; OFF clears the command and drives the GPIO off right away
 * so the LED does not stay lit until the next state-machine pass. */
static void led_handler_apply_cmd(led_handler_t *self, const led_queue_cmd_t *cmd)
{
    uint8_t label = cmd->led_lable;
    led_control_t *table = TABLE(self);

    if (label >= TABLE_NUM(self))
    {
        return;
    }

    if (LED_CMD_ON == cmd->cmd)
    {
        PRIV_DATA(self)->led_status[label].led_cmd            = LED_CMD_ON;
        PRIV_DATA(self)->led_status[label].cur_level          = 1;
        PRIV_DATA(self)->led_status[label].remain_flash_cnt   = table[label].flash_cnt;
        PRIV_DATA(self)->led_status[label].remain_period_tick = table[label].flash_period_tick;
    }
    else /* LED_CMD_OFF */
    {
        PRIV_DATA(self)->led_status[label].led_cmd = LED_CMD_OFF;
        for (uint8_t j = 0; j < table[label].led_level; j++)
        {
            table[label].led_ops[j].pf_led_off();
        }
    }
}
#endif /* LED_HANDLER_USE_QUEUE_MODE */

static void led_handler_thread(void *arg)
{
    led_handler_t *self = (led_handler_t *)arg;
    led_control_t *table = TABLE(self);
    uint8_t table_num = TABLE_NUM(self);
    for (;;)  /* Infinite task loop */
    {
#ifdef LED_HANDLER_USE_QUEUE_MODE
        /* Drain every pending command before evaluating the state machine.
         * Non-blocking (timeout 0): we only consume what is already queued,
         * then fall through to drive the active LED. Because the handler
         * thread is the sole owner of led_status[], no critical section is
         * required here. */
        {
            led_queue_cmd_t qcmd;
            while (0 == OS_IF(self)->pf_os_queue_receive(
                        PRIV_DATA(self)->queue_handle, &qcmd, 0U))
            {
                led_handler_apply_cmd(self, &qcmd);
            }
        }
#endif /* LED_HANDLER_USE_QUEUE_MODE */

        /* Find highest priority active LED (first active LED in table) */
        int8_t active_index = -1;
        for(uint8_t i=0; i<table_num; i++)
        {
            if(LED_CMD_ON == PRIV_DATA(self)->led_status[i].led_cmd)
            {
                active_index = i;
                break;  /* Take first active LED (priority by table order) */
            }
        }

        /* Process active LED if found */
        if(-1 != active_index)
        {
            LED_CS_DECL(self);

            /* The whole state read / GPIO / state write block runs in a
             * critical section (original mode) so led_ctrl() cannot preempt
             * mid-update and leave the state half-written. The GPIO calls are
             * single register writes, so the added interrupt latency is
             * negligible. In queue mode the handler thread already owns the
             * state and LED_CS_ENTER/EXIT compile to nothing. */
            LED_CS_ENTER(self);

            /* Sanity check for current level (prevent out-of-bounds) */
            if(PRIV_DATA(self)->led_status[active_index].cur_level > LED_OPS_LEVEL_MAX)
            {
                PRIV_DATA(self)->led_status[active_index].cur_level = LED_OPS_LEVEL_MAX;
            }

            /* Dynamic tick: 1/10 of the active LED's flash period. Both the
             * delay and the decrement below use this same value so the duty
             * cycle ratio stays correct regardless of the period. Clamp to a
             * minimum of 1 to avoid a zero-delay (busy-wait) loop. */
            uint32_t tick = table[active_index].flash_period_tick / 10U;
            if (tick == 0U)
            {
                tick = 1U;
            }

            /* Calculate duty cycle threshold (ON/OFF decision) */
            uint32_t duty_threshold = (uint32_t)(table[active_index].flash_period_tick) * (100 - table[active_index].flash_duty) / 100;

            /* Control LED based on duty cycle */
            if(PRIV_DATA(self)->led_status[active_index].remain_period_tick > duty_threshold)
            {
                table[active_index].led_ops[PRIV_DATA(self)->led_status[active_index].cur_level - 1].pf_led_on();
            }
            else
            {
                table[active_index].led_ops[PRIV_DATA(self)->led_status[active_index].cur_level - 1].pf_led_off();
            }

            /* Decrement remaining period ticks */
            if(PRIV_DATA(self)->led_status[active_index].remain_period_tick > tick)
            {
                PRIV_DATA(self)->led_status[active_index].remain_period_tick -= tick;
            }
            else
            {
                /* Reset period when tick count exhausted */
                if(PRIV_DATA(self)->led_status[active_index].remain_flash_cnt > 1 &&
                   FLASH_CNT_MAX != PRIV_DATA(self)->led_status[active_index].remain_flash_cnt)
                {
                    PRIV_DATA(self)->led_status[active_index].remain_flash_cnt--;
                }
                else if(1 == PRIV_DATA(self)->led_status[active_index].remain_flash_cnt)
                {
                    /* Disable LED when flash count completes */
                    PRIV_DATA(self)->led_status[active_index].led_cmd = LED_CMD_OFF;
                    table[active_index].led_ops[PRIV_DATA(self)->led_status[active_index].cur_level - 1].pf_led_off();
                    LED_CS_EXIT(self);
                    continue;  /* Skip level increment if done */
                }

                /* Increment level (cycle through LED levels) */
                PRIV_DATA(self)->led_status[active_index].cur_level++;
                if (PRIV_DATA(self)->led_status[active_index].cur_level > table[active_index].led_level)
                {
                    PRIV_DATA(self)->led_status[active_index].cur_level = 1;  /* Wrap to first level */
                }

                /* Reset period tick counter */
                PRIV_DATA(self)->led_status[active_index].remain_period_tick = table[active_index].flash_period_tick;
            }

            LED_CS_EXIT(self);

            /* Task delay matches the dynamic tick (1/10 of flash period). */
            OS_IF(self)->pf_os_delay(tick);
        }
#ifdef LED_HANDLER_USE_SEMA_BLOCK
        else
        {
            /* No LED active: block on event-gate semaphore until led_ctrl()
             * turns one on and gives the semaphore. Consumes no CPU while waiting. */
            OS_IF(self)->pf_os_sema_take(PRIV_DATA(self)->sema_handle, LED_SEMA_TIMEOUT_MAX);
        }
#endif
    }
}

/* Public Instantiate Function --------------------------------------------*/
/**
 * @brief Instantiate LED handler
 * Validates configuration, allocates memory, creates control task
 * @param table Pointer to LED control configuration table
 * @return led_handler_status_t Status code (OK = success)
 */
led_handler_status_t led_handler_inst(led_handler_t *const self, led_input_arg_t *const p_input_args)
{
    /* Input parameter validation */
    if (!self || !p_input_args || !p_input_args->table ||
        (0 == p_input_args->table_num) || !p_input_args->os_if ||
        !p_input_args->os_if->pf_os_thread_create || !p_input_args->os_if->pf_os_delay)
    {
        return LED_HANDLER_ERR_PARAM_INVALID;
    }

#ifdef LED_HANDLER_USE_QUEUE_MODE
    /* Queue mode: the handler thread owns led_status[] exclusively, so we need
     * the queue API instead of the critical-section API. */
    if (!p_input_args->os_if->pf_os_queue_create ||
        !p_input_args->os_if->pf_os_queue_delete ||
        !p_input_args->os_if->pf_os_queue_send ||
        !p_input_args->os_if->pf_os_queue_receive)
    {
        return LED_HANDLER_ERR_PARAM_INVALID;
    }
#else
    if (!p_input_args->os_if->pf_os_enter_critical || !p_input_args->os_if->pf_os_exit_critical)
    {
        return LED_HANDLER_ERR_PARAM_INVALID;
    }
#endif

#ifdef LED_HANDLER_USE_SEMA_BLOCK
    /* Validate semaphore function pointers */
    if (!p_input_args->os_if->pf_os_sema_binary_create ||
        !p_input_args->os_if->pf_os_sema_delete ||
        !p_input_args->os_if->pf_os_sema_give ||
        !p_input_args->os_if->pf_os_sema_take)
    {
        return LED_HANDLER_ERR_PARAM_INVALID;
    }
#endif

    /*
     * Duplicate-initialization guard.
     *
     * Must run BEFORE the memset below. We only inspect self->magic here — a
     * single native-width read that never dereferences priv_data. This makes
     * the check safe even when self lives on the stack / in freshly malloc'd
     * memory whose priv_data still holds a stale garbage value.
     */
    if (self->magic == LED_HANDLER_MAGIC)
    {
        /* Magic is present; private data must be valid and marked initialized. */
        if (PRIV_DATA(self) && PRIV_DATA(self)->if_init)
        {
            return LED_HANDLER_ERR_ALREADY_INITED;
        }
    }

    /*
     * Zero the instance. This neutralises any wild pointer left in priv_data
     * (or any other field) by the caller's storage class before we start
     * dereferencing anything. Safe now because the duplicate-init guard above
     * has already captured the "already initialised" state.
     */
    memset(self, 0, sizeof(*self));

    /* Save input arguments pointer */
    self->input_args = p_input_args;

    /* Validate control table entries */
    for (uint8_t i = 0; i < TABLE_NUM(self); i++)
    {
        /* Check LED level bounds */
        if(0 == TABLE(self)[i].led_level || TABLE(self)[i].led_level > LED_OPS_LEVEL_MAX)
        {
            return LED_HANDLER_ERR_PARAM_INVALID;
        }

        /* Validate LED operation callbacks */
        for(uint8_t j=0; j<TABLE(self)[i].led_level; j++)
        {
            if (!TABLE(self)[i].led_ops[j].pf_led_on || !TABLE(self)[i].led_ops[j].pf_led_off)
            {
                return LED_HANDLER_ERR_PARAM_INVALID;
            }
        }        
    }

    /* Allocate handler context */
    PRIV_DATA(self) = MALLOC(sizeof(led_handler_priv_data_t));
    if(!PRIV_DATA(self))
    {
        return LED_HANDLER_ERR_OTHERS;
    }

    /* Allocate LED status array */
    PRIV_DATA(self)->led_status = MALLOC(sizeof(led_pri_status_t) * TABLE_NUM(self));
    if(!PRIV_DATA(self)->led_status)
    {
        FREE(PRIV_DATA(self));  /* Cleanup on allocation failure */
        PRIV_DATA(self) = NULL;
        return LED_HANDLER_ERR_OTHERS;
    }

    /* Initialize LED status to default */
    memset(PRIV_DATA(self)->led_status, 0, sizeof(led_pri_status_t) * TABLE_NUM(self));

    for (uint8_t i = 0; i < TABLE_NUM(self); i++)
    {
        PRIV_DATA(self)->led_status[i].led_cmd              = LED_CMD_OFF;
        PRIV_DATA(self)->led_status[i].cur_level            = 1;
        PRIV_DATA(self)->led_status[i].remain_flash_cnt     = TABLE(self)[i].flash_cnt;
        PRIV_DATA(self)->led_status[i].remain_period_tick   = TABLE(self)[i].flash_period_tick;

        /* Turn off LEDs */
        for(uint8_t j=0; j<TABLE(self)[i].led_level; j++)
        {
            TABLE(self)[i].led_ops[j].pf_led_off();
        }
    }

#ifdef LED_HANDLER_USE_SEMA_BLOCK
    /* Create event-gate semaphore (binary). Initial state = taken (unavailable)
     * so the handler thread blocks until the first LED is turned on. */
    {
        int32_t ret = OS_IF(self)->pf_os_sema_binary_create(&PRIV_DATA(self)->sema_handle);
        if (ret != 0 || !PRIV_DATA(self)->sema_handle)
        {
            FREE(PRIV_DATA(self)->led_status);
            FREE(PRIV_DATA(self));
            PRIV_DATA(self) = NULL;
            return LED_HANDLER_ERR_OTHERS;
        }
        /* Flush: ensure semaphore starts in the "not given" state */
        OS_IF(self)->pf_os_sema_take(PRIV_DATA(self)->sema_handle, 0);
    }
#endif

#ifdef LED_HANDLER_USE_QUEUE_MODE
    /* Create the command queue. Depth LED_HANDLER_QUEUE_DEPTH is large enough
     * to absorb a full led_all_off() burst (one OFF command per LED label)
     * without blocking the caller. */
    {
        int32_t ret = OS_IF(self)->pf_os_queue_create(
            LED_HANDLER_QUEUE_DEPTH, sizeof(led_queue_cmd_t), &PRIV_DATA(self)->queue_handle);
        if (ret != 0 || !PRIV_DATA(self)->queue_handle)
        {
            FREE(PRIV_DATA(self)->led_status);
            FREE(PRIV_DATA(self));
            PRIV_DATA(self) = NULL;
            return LED_HANDLER_ERR_OTHERS;
        }
    }
#endif

    /* Create OS task for LED control */
    void *thread_handle = NULL;
    int32_t ret = OS_IF(self)->pf_os_thread_create(
        "LED_HANDLER",
        led_handler_thread,
        LED_HANDLER_THREAD_STACK_DEPTH,
        LED_HANDLER_THREAD_PRIORITY,
        &thread_handle,
        (void*)self
    );
    
    if (ret != 0 || !thread_handle)
    {     
        FREE(PRIV_DATA(self)->led_status);
        FREE(PRIV_DATA(self));
        PRIV_DATA(self) = NULL;
        return LED_HANDLER_ERR_OTHERS;  /* Task creation failure */        
    }
    
    /* Save thread handle */
    PRIV_DATA(self)->thread_handle = thread_handle;

    /* Finalize initialization.
     * magic is stamped last so a partially-initialised instance (e.g. one that
     * bails out due to an allocation failure mid-way) is never mistaken for a
     * live one by the duplicate-init guard.
     */
    PRIV_DATA(self)->if_init = true;
    self->magic = LED_HANDLER_MAGIC;

    return LED_HANDLER_OK;
}

/* Public LED Control Function -----------------------------------------------*/
/**
 * @brief Control LED state (ON/OFF) by label
 * Updates LED command and resets flash parameters
 * @param led_lable Target LED label (from led_lable_t)
 * @param led_cmd Control command (LED_CMD_ON/LED_CMD_OFF)
 * @return led_handler_status_t Status code (OK = success)
 */
led_handler_status_t led_ctrl(led_handler_t *const self, uint8_t led_lable, led_cmd_t led_cmd)
{
    /* Input validation */


    if(!self || !PRIV_DATA(self) || !PRIV_DATA(self)->if_init)
    {
        return LED_HANDLER_ERR_HANDLER_NOT_READY;
    }
    if(led_lable >= TABLE_NUM(self))
    {
        return LED_HANDLER_ERR_PARAM_INVALID;
    }

#ifdef LED_HANDLER_USE_QUEUE_MODE
    /* Queue mode: never touch led_status[] here. Post the command and let the
     * handler thread (the sole owner) apply it. This removes the critical
     * section entirely. */
    {
        led_queue_cmd_t qcmd = { .led_lable = led_lable, .cmd = led_cmd };
        if (0 != OS_IF(self)->pf_os_queue_send(PRIV_DATA(self)->queue_handle, &qcmd, LED_SEMA_TIMEOUT_MAX))
        {
            return LED_HANDLER_ERR_OTHERS;
        }
    }

    if(LED_CMD_ON == led_cmd)
    {
#ifdef LED_HANDLER_USE_SEMA_BLOCK
        /* Wake the handler thread if it was blocked waiting for an active LED */
        OS_IF(self)->pf_os_sema_give(PRIV_DATA(self)->sema_handle);
#endif
    }
    /* NOTE: the OFF path does NOT drive the GPIO here. In queue mode the handler
     * thread turns the LED off when it drains the command (see
     * led_handler_apply_cmd), so the LED is guaranteed to go off even if this
     * caller is preempted before it could reach the GPIO loop. */
#else
    /* Original (shared-state) mode: update led_status[] under a critical section
     * so the handler thread never sees a half-updated state (e.g. led_cmd == ON
     * but the flash counters still hold stale values from the previous cycle). */
    uint32_t primask = OS_IF(self)->pf_os_enter_critical();
    PRIV_DATA(self)->led_status[led_lable].led_cmd = led_cmd;

    /* Reset flash parameters when turning ON */
    if(LED_CMD_ON == led_cmd)
    {
        PRIV_DATA(self)->led_status[led_lable].cur_level = 1;
        PRIV_DATA(self)->led_status[led_lable].remain_flash_cnt = TABLE(self)[led_lable].flash_cnt;
        PRIV_DATA(self)->led_status[led_lable].remain_period_tick = TABLE(self)[led_lable].flash_period_tick;
    }
    OS_IF(self)->pf_os_exit_critical(primask);

    if(LED_CMD_ON == led_cmd)
    {
#ifdef LED_HANDLER_USE_SEMA_BLOCK
        /* Wake the handler thread if it was blocked waiting for an active LED */
        OS_IF(self)->pf_os_sema_give(PRIV_DATA(self)->sema_handle);
#endif
    }
    else if(LED_CMD_OFF == led_cmd) /* Turn off LED immediately when command is OFF */
    {
        for(uint8_t j=0; j<TABLE(self)[led_lable].led_level; j++)
        {
            TABLE(self)[led_lable].led_ops[j].pf_led_off();
        }
    }
#endif /* LED_HANDLER_USE_QUEUE_MODE */

    return LED_HANDLER_OK;
}

/* Public LED Control Function -----------------------------------------------*/
/**
 * @brief Turn off all LEDs immediately
 * Clears all LED commands to OFF and drives every LED output off
 * @return led_handler_status_t Status code (OK = success)
 */
led_handler_status_t led_all_off(led_handler_t *const self)
{
    /* Input validation */
    if(!self || !PRIV_DATA(self) || !PRIV_DATA(self)->if_init)
    {
        return LED_HANDLER_ERR_HANDLER_NOT_READY;
    }

#ifdef LED_HANDLER_USE_QUEUE_MODE
    /* Queue mode: post one OFF command per label. The handler thread applies
     * each in turn (clearing led_cmd and driving the GPIO off). LED_HANDLER_QUEUE_DEPTH
     * is sized to hold a full burst, so osal_queue_send will not overflow. */
    for(uint8_t i=0; i<TABLE_NUM(self); i++)
    {
        led_queue_cmd_t qcmd = { .led_lable = i, .cmd = LED_CMD_OFF };
        if (0 != OS_IF(self)->pf_os_queue_send(PRIV_DATA(self)->queue_handle, &qcmd, LED_SEMA_TIMEOUT_MAX))
        {
            return LED_HANDLER_ERR_OTHERS;
        }
    }
    /* No semaphore give: an all-off burst makes every LED inactive, so the
     * handler thread has no reason to wake. If it is already awake (some LED
     * was on), it will drain the queue on its next pass. */
#else
    /* Original (shared-state) mode: clear each led_cmd under a critical section
     * so the handler thread cannot act on a partially-cleared state. */
    for(uint8_t i=0; i<TABLE_NUM(self); i++)
    {
        uint32_t primask = OS_IF(self)->pf_os_enter_critical();
        /* Clear command so the handler thread will not turn it back on */
        PRIV_DATA(self)->led_status[i].led_cmd = LED_CMD_OFF;
        OS_IF(self)->pf_os_exit_critical(primask);

        /* Drive all levels off */
        for(uint8_t j=0; j<TABLE(self)[i].led_level; j++)
        {
            TABLE(self)[i].led_ops[j].pf_led_off();
        }
    }
#endif /* LED_HANDLER_USE_QUEUE_MODE */

    return LED_HANDLER_OK;
}
