/**
 * @file led_handler.h
 * @brief LED Control Handler Header
 * @author Donzel
 * @version 1.0.0
 * @date 2025-12-22
 *
 * Core APIs and data structures for LED control (priority-based state management,
 * multi-group isolation, RTOS integration).
 */

#ifndef __LED_HANDLER_H__
#define __LED_HANDLER_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* Configuration Macros ------------------------------------------------------*/  
/* Maximum flash count (use for infinite flashing) */
#define FLASH_CNT_MAX       0xFFFF

/* Maximum number of LED operation levels */
#define LED_OPS_LEVEL_MAX   2    

/* Thread configuration for LED parsing task */
#define LED_HANDLER_THREAD_PRIORITY         9  /* Priority of LED control thread */
#define LED_HANDLER_THREAD_STACK_DEPTH      0x200             /* Stack size for LED control thread */


/**
 * @def LED_HANDLER_USE_QUEUE_MODE
 * @brief Switches the LED handler from the default shared-state design to a
 *        queue-based design.
 *
 * When undefined (default): original design. led_ctrl() / led_all_off() write
 * led_status[] directly under a critical section; the handler thread reads the
 * same shared state. Simple and proven.
 *
 * When defined: queue-based design. led_ctrl() / led_all_off() post commands to
 * a queue instead of touching led_status[]. The handler thread becomes the sole
 * owner of led_status[]: it drains the queue, then runs the state machine. No
 * critical section is needed, so there is zero interrupt-latency cost and thread
 * safety is guaranteed by construction (the same pattern as the RFID handler).
 */
// #define LED_HANDLER_USE_QUEUE_MODE
#ifdef LED_HANDLER_USE_QUEUE_MODE
/* Queue command depth (used only when LED_HANDLER_USE_QUEUE_MODE is defined).
 * Must be >= the number of LED labels so that led_all_off() can post one OFF
 * command per label without blocking. */
#define LED_HANDLER_QUEUE_DEPTH    4U
#endif

/* Debug output macro (disabled by default) */
#define LED_HANDLER_DEBUG_OUT(fmt, ...)

/**
 * @def LED_HANDLER_USE_SEMA_BLOCK
 * @brief Controls whether the LED handler thread blocks via semaphore when all
 *        LEDs are off.
 *
 * When defined (default), the thread takes a binary semaphore and blocks
 * indefinitely when no LED is active, so it consumes no CPU. led_ctrl() gives
 * the semaphore to wake the thread when an LED is turned on.
 *
 * When undefined, the thread falls back to the original behaviour: it loops
 * continuously with a dynamic pf_os_delay() (flash_period_tick / 10) regardless
 * of whether any LED is active.
 */
#define LED_HANDLER_USE_SEMA_BLOCK

/** Timeout value passed to pf_os_sema_take() = block indefinitely */
#define LED_SEMA_TIMEOUT_MAX                0xFFFFFFFFU

/* OS Interface Abstraction ------------------------------------------------__*/
/* OS operations interface for RTOS decoupling */
typedef struct
{
    int32_t (*pf_os_thread_create)(const char *name,
                                   void (*task)(void *),
                                   size_t stack_size,
                                   uint32_t priority,
                                   void **handle,
                                   void *arg);
    void (*pf_os_thread_delete)(void *const handle);
    void (*pf_os_delay)(int32_t ticks);

#ifdef LED_HANDLER_USE_QUEUE_MODE
    /* Queue: led_ctrl()/led_all_off() post commands here; the handler thread —
     * the sole owner of led_status[] — drains them. No critical section needed. */
    int32_t (*pf_os_queue_create)(size_t queue_depth, size_t data_size, void **p_queue_handle);
    void    (*pf_os_queue_delete)(void *queue_handle);
    int32_t (*pf_os_queue_send)(void *queue_handle, const void *data, uint32_t timeout);
    int32_t (*pf_os_queue_receive)(void *queue_handle, void *data, uint32_t timeout);
#else
    /* Critical section: enter/exit to make multi-field led_status accesses atomic
     * against concurrent led_ctrl()/led_all_off() calls. */
    uint32_t (*pf_os_enter_critical)(void);
    void     (*pf_os_exit_critical)(uint32_t primask);
#endif

#ifdef LED_HANDLER_USE_SEMA_BLOCK
    /* Event-gate semaphore: blocks the thread when no LED is active */
    int32_t (*pf_os_sema_binary_create)(void **p_sema_handle);
    void    (*pf_os_sema_delete)(void *sema_handle);
    int32_t (*pf_os_sema_give)(void *sema_handle);
    int32_t (*pf_os_sema_take)(void *sema_handle, uint32_t timeout);
#endif
} led_os_interface_t;

/**
 * @def LED_HANDLER_MAGIC
 * @brief Magic marker stamped on a fully-initialized led_handler_t instance.
 *
 * Lets led_handler_inst() distinguish a live instance from one that is still
 * zero-initialised or holds a stale wild pointer in priv_data, without ever
 * dereferencing that (possibly invalid) private-data pointer.
 */
#define LED_HANDLER_MAGIC               0x4C454448U  /* "LEDH" */

/* Status Codes --------------------------------------------------------------*/
typedef enum
{
    LED_HANDLER_OK = 0,                      /* Operation completed successfully */
    LED_HANDLER_ERR_PARAM_INVALID,           /* Invalid input parameter(s) */
    LED_HANDLER_ERR_ALREADY_INITED,          /* Handler has already been initialized */
    LED_HANDLER_ERR_HANDLER_NOT_READY,       /* LED handler not initialized */
    LED_HANDLER_ERR_OTHERS                   /* Unspecified error */
} led_handler_status_t;

/* LED Operation Interface ---------------------------------------------------*/
/* Hardware-specific LED control functions (implemented by user) */
typedef struct led
{
    void (*pf_led_on)(void);    /* Callback to turn LED on */
    void (*pf_led_off)(void);   /* Callback to turn LED off */
} led_ops_t;

/* LED Control Commands ------------------------------------------------------*/
typedef enum
{
    LED_CMD_OFF = 0,    /* Command to turn LED off */
    LED_CMD_ON,         /* Command to turn LED on */
} led_cmd_t;

/* LED Control Table Structure -----------------------------------------------*/
/* Table-driven LED configuration structure(lower index = higher priority */
typedef struct
{
    uint8_t     led_lable;              /* LED event label */
    uint8_t     led_level;              /* LED operation level index */
    led_ops_t   led_ops[LED_OPS_LEVEL_MAX]; /* LED control callbacks per level */  
    uint8_t     flash_duty;             /* Flash duty cycle (0~100%) */
    uint16_t    flash_period_tick;      /* Flash period in system ticks */
    uint16_t    flash_cnt;              /* Flash count (use FLASH_CNT_MAX for infinite) */
} led_control_t;

typedef struct led_handler_priv_data led_handler_priv_data_t;

/* LED Handler Input Arguments -----------------------------------------------*/
/* Input arguments for LED handler instantiation */
typedef struct
{
    led_control_t      *table;      /* Pointer to LED control configuration table */
    uint8_t            table_num;   /* Number of entries in control table */
    led_os_interface_t *os_if;      /* OS interface for RTOS operations */
} led_input_arg_t;

typedef struct
{
    uint32_t                magic;        /* Set to LED_HANDLER_MAGIC only after full init succeeds */
    led_handler_priv_data_t *priv_data;
    led_input_arg_t         *input_args;  /* Input arguments (table, os_if, etc.) */
} led_handler_t;

/* Public API Functions ------------------------------------------------------*/
/* Instantiate LED handler with input arguments */
led_handler_status_t led_handler_inst(led_handler_t *const self, 
                                       led_input_arg_t *const p_input_args);

/* Control LED state by label and command */
led_handler_status_t led_ctrl(led_handler_t *const self, uint8_t led_lable, led_cmd_t led_cmd);

/* Turn off all LEDs immediately */
led_handler_status_t led_all_off(led_handler_t *const self);

#endif //__LED_HANDLER_H__
