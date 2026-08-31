/**
 * @file switch_handler.h
 * @brief Generic switch / button edge detector.
 *
 * A switch instance owns only OS infrastructure (a queue, a processing thread,
 * and a tick source). Application callbacks are attached dynamically as
 * "listeners" through switch_register(), so a single physical switch can be
 * observed by any number of listeners at once, in any combination of modes:
 *
 *   - SWITCH_CB_EDGE : an edge occurred                  -> cb(edge)
 *   - SWITCH_CB_PRESS: a press completed (short/long)    -> cb(press_type)
 *
 * The same mode may be registered more than once (e.g. two PRESS listeners
 * with different timing thresholds), each acting independently.
 *
 * Lifecycle:
 *   switch_inst()        -> create instance (no listeners yet)
 *   switch_register()    -> attach one listener, returns an opaque handle
 *   switch_unregister()  -> detach a listener by its handle
 *
 * Before any listener is registered the instance silently consumes edge events.
 *
 * Timing thresholds are per-listener parameters in milliseconds. The handler
 * converts tick deltas to milliseconds through switch_os_interface_t.pf_tick_2_ms,
 * staying independent of the actual tick rate.
 *
 * Platform-agnostic: all OS/HAL dependencies are injected through
 * switch_os_interface_t. Multi-instance: private data is heap-allocated per
 * instance, so any number of switches can be tracked independently.
 */

#ifndef __SWITCH_HANDLER_H__
#define __SWITCH_HANDLER_H__

#include <stdint.h>
#include <stdbool.h>
#include "osal.h"

/* -------------------------------------------------------------------------- */
/*                          Tick width configuration                          */
/* -------------------------------------------------------------------------- */

#ifndef SWITCH_TICK_TYPE
#define SWITCH_TICK_TYPE      uint32_t
#endif

/* -------------------------------------------------------------------------- */
/*                        Listener table configuration                        */
/* -------------------------------------------------------------------------- */

#ifndef SWITCH_MAX_LISTENERS
#define SWITCH_MAX_LISTENERS                    2
#endif

/* -------------------------------------------------------------------------- */
/*                          Handler thread resources                          */
/* -------------------------------------------------------------------------- */

#define SWITCH_HANDLER_THREAD_PRIORITY          8
#define SWITCH_HANDLER_THREAD_STACK_DEPTH       1024
#define SWITCH_HANDLER_QUEUE_DEPTH              2

/* -------------------------------------------------------------------------- */
/*                              Callback types                                */
/* -------------------------------------------------------------------------- */

typedef enum
{
    SWITCH_CB_EDGE = 0,   /* cb(edge)                                 */
    SWITCH_CB_PRESS,      /* cb(press_type)                           */
} switch_cb_type_t;

typedef enum
{
    SWITCH_EDGE_RISING = 0,
    SWITCH_EDGE_FALLING,
} switch_edge_t;

typedef enum
{
    SWITCH_PRESS_SHORT = 0,
    SWITCH_PRESS_LONG,
} switch_press_type_t;

typedef void (*switch_edge_cb_t)(switch_edge_t edge);
typedef void (*switch_press_cb_t)(switch_press_type_t press_type);

/* -------------------------------------------------------------------------- */
/*                        Per-mode register arguments                         */
/* -------------------------------------------------------------------------- */

/* SWITCH_CB_EDGE : report every debounced edge. */
typedef struct
{
    switch_edge_cb_t  edge_cb;          /* cb(edge)                         */
    uint32_t          debounce_ms;      /* reject edges within this window  */
} switch_edge_arg_t;

/* SWITCH_CB_PRESS: report short / long at release. */
typedef struct
{
    switch_press_cb_t press_cb;            /* cb(press_type)                */
    switch_edge_t     start_edge;          /* edge that begins a press      */
    uint32_t          debounce_ms;         /* reject shorter than this      */
    uint32_t          short_press_max_ms;  /* <= this = short press         */
    uint32_t          long_press_min_ms;   /* >= this = long press          */
} switch_press_arg_t;

typedef union
{
    switch_edge_arg_t  edge;
    switch_press_arg_t press;
} switch_arg_u;

typedef enum
{
    SWITCH_HANDLER_OK = 0,
    SWITCH_HANDLER_ERR_PARAM_INVALID,
    SWITCH_HANDLER_ERR_NOT_INITED,
    SWITCH_HANDLER_ERR_ALREADY_INITED,
    SWITCH_HANDLER_ERR_FULL,            /* no free listener slot              */
    SWITCH_HANDLER_ERR_NOT_FOUND,       /* unregister: handle not registered  */
    SWITCH_HANDLER_ERR_OTHERS,
} switch_handler_status_t;

/* -------------------------------------------------------------------------- */
/*                          OS-abstraction interface                          */
/* -------------------------------------------------------------------------- */

typedef struct
{
    SWITCH_TICK_TYPE (*pf_get_tick)(void);
    uint32_t         (*pf_tick_2_ms)(SWITCH_TICK_TYPE tick);  /* ticks -> ms  */

    int32_t  (*pf_queue_create)(size_t queue_depth, size_t data_size, void **p_queue_handle);
    void     (*pf_queue_delete)(void *queue_handle);
    int32_t  (*pf_queue_send)(void *queue_handle, const void *data, uint32_t timeout);
    int32_t  (*pf_queue_receive)(void *queue_handle, void *data, uint32_t timeout);

    int32_t  (*pf_thread_create)(const char *name, void (*entry)(void *), size_t stack_size,
                                 uint32_t priority, void **p_handle, void *arg);
    void     (*pf_delay_ms)(uint32_t ms);
} switch_os_interface_t;

/* -------------------------------------------------------------------------- */
/*                          Opaque listener handle                            */
/* -------------------------------------------------------------------------- */

typedef struct switch_listener switch_listener_t;

typedef struct switch_handler_priv_data switch_handler_priv_data_t;

typedef struct
{
    uint32_t                    magic;          /* SWITCH_HANDLER_MAGIC after init */
    switch_handler_priv_data_t  *priv_data;     /* Private runtime data            */
} switch_handler_t;

/* -------------------------------------------------------------------------- */
/*                              Public API                                    */
/* -------------------------------------------------------------------------- */

switch_handler_status_t switch_inst(switch_handler_t *self,
                                    const switch_os_interface_t *os_if);

/* Attach one listener. *out_handle receives an opaque handle to pass later to
 * switch_unregister(). The arg union member must match cb_type. */
switch_handler_status_t switch_register(switch_handler_t *self,
                                        switch_cb_type_t cb_type,
                                        const switch_arg_u *arg,
                                        switch_listener_t **out_handle);

/* Detach a listener previously returned by switch_register(). */
switch_handler_status_t switch_unregister(switch_handler_t *self,
                                          switch_listener_t *handle);

/* ISR-side: call from HAL_GPIO_EXTI_Rising_Callback / _Falling_Callback. */
void switch_handler_rising_callback(switch_handler_t *self);
void switch_handler_falling_callback(switch_handler_t *self);

#endif /* __SWITCH_HANDLER_H__ */
