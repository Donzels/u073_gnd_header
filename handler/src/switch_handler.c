/**
 * @file switch_handler.c
 * @brief Generic switch / button edge detector implementation.
 *
 * Owns OS infrastructure only (queue + thread + tick source). Application
 * callbacks are attached as listeners via switch_register(); any number and
 * combination of modes (EDGE / PRESS / HOLD) may observe one switch at once,
 * and the same mode may be registered repeatedly with independent timing.
 *
 * Timing thresholds are per-listener (milliseconds). Durations measured in
 * ticks are converted to milliseconds through switch_os_interface_t.pf_tick_2_ms
 * so the handler stays independent of the actual tick rate.
 *
 * Every OS/HAL dependency is injected through switch_os_interface_t. The core
 * never touches OSAL / HAL directly. Multi-instance via heap-allocated private
 * data, so any number of switches run independently.
 */

#include "switch_handler.h"

#include <stdlib.h>
#include <stdint.h>
#define MALLOC(size)        malloc(size)
#define FREE(ptr)           free(ptr)

#define SWITCH_HANDLER_MAGIC              0x53574954U  /* "SWIT" */

#define SWITCH_SUCCESS    (0)

#define PRIV_DATA(p)    (p)->priv_data

/* -------------------------------------------------------------------------- */
/*                              Queue payload                                  */
/* -------------------------------------------------------------------------- */

typedef struct
{
    switch_edge_t     edge;
    SWITCH_TICK_TYPE  tick;
} switch_priv_event_t;

/* -------------------------------------------------------------------------- */
/*                              Listener instance                              */
/* -------------------------------------------------------------------------- */

struct switch_listener
{
    bool              used;     /* slot occupied                       */
    switch_cb_type_t  type;     /* EDGE / PRESS                        */
    union
    {
        switch_edge_cb_t  edge_cb;
        switch_press_cb_t press_cb;
    } cb;
    union
    {
        struct
        {
            uint32_t          debounce_ms;
            SWITCH_TICK_TYPE  last_tick;  /* per-listener EDGE debounce      */
        } edge;
        struct
        {
            switch_edge_t    start_edge;
            uint32_t         debounce_ms;
            uint32_t         short_press_max_ms;
            uint32_t         long_press_min_ms;
        } press;
    } cfg;
};

/* -------------------------------------------------------------------------- */
/*                              Private data                                   */
/* -------------------------------------------------------------------------- */

typedef struct switch_handler_priv_data
{
    bool                          if_init;
    SWITCH_TICK_TYPE              falling_tick;     /* last falling edge             */
    SWITCH_TICK_TYPE              rising_tick;      /* last rising edge              */
    bool                          falling_armed;    /* falling -> rising in progress */
    bool                          rising_armed;     /* rising  -> falling in progress*/
    const switch_os_interface_t  *os_if;
    void                          *queue_handle;
    void                          *thread_handle;
    switch_listener_t             listeners[SWITCH_MAX_LISTENERS];
} switch_handler_priv_data_t;

/* -------------------------------------------------------------------------- */
/*                              Tick source                                    */
/* -------------------------------------------------------------------------- */

static SWITCH_TICK_TYPE switch_get_tick(const switch_handler_t *self)
{
    return (SWITCH_TICK_TYPE)PRIV_DATA(self)->os_if->pf_get_tick();
}

/* Convert a tick delta to milliseconds through the OS interface. */
static uint32_t switch_tick_2_ms(const switch_handler_t *self, SWITCH_TICK_TYPE delta)
{
    return PRIV_DATA(self)->os_if->pf_tick_2_ms(delta);
}

/* -------------------------------------------------------------------------- */
/*                              Classification                                 */
/* -------------------------------------------------------------------------- */

static bool classify_press(uint32_t duration_ms, uint32_t short_press_max_ms,
                           uint32_t long_press_min_ms, switch_press_type_t *out_type)
{
    if (duration_ms <= short_press_max_ms)
    {
        *out_type = SWITCH_PRESS_SHORT;
        return true;
    }
    if (duration_ms >= long_press_min_ms)
    {
        *out_type = SWITCH_PRESS_LONG;
        return true;
    }
    /* Ambiguous band: drop. */
    return false;
}

/* -------------------------------------------------------------------------- */
/*                        Push event (ISR -> queue)                            */
/* -------------------------------------------------------------------------- */

static void switch_push_event(switch_handler_t *self, switch_edge_t edge)
{
    const switch_os_interface_t *os_if = PRIV_DATA(self)->os_if;
    switch_priv_event_t ev;

    ev.edge = edge;
    ev.tick = switch_get_tick(self);

    /* ISR context: no-wait send. */
    os_if->pf_queue_send(PRIV_DATA(self)->queue_handle, &ev, 0U);
}

/* -------------------------------------------------------------------------- */
/*                        Handler processing thread                            */
/* -------------------------------------------------------------------------- */

static void switch_handler_thread(void *arg)
{
    switch_handler_t *self = (switch_handler_t *)arg;
    switch_priv_event_t ev;

    for (;;)
    {
        if (PRIV_DATA(self)->os_if->pf_queue_receive(
                PRIV_DATA(self)->queue_handle, &ev, UINT32_MAX) != SWITCH_SUCCESS)
        {
            continue;
        }

        switch_edge_t edge = ev.edge;
        SWITCH_TICK_TYPE now = ev.tick;
        switch_handler_priv_data_t *priv = PRIV_DATA(self);

        /* Record this edge and arm its direction for the opposite-edge logic. */
        if (edge == SWITCH_EDGE_FALLING)
        {
            priv->falling_tick  = now;
            priv->falling_armed = true;
        }
        else
        {
            priv->rising_tick  = now;
            priv->rising_armed = true;
        }

        /* Notify every registered listener. */
        for (uint32_t i = 0; i < SWITCH_MAX_LISTENERS; i++)
        {
            switch_listener_t *L = &priv->listeners[i];
            if (!L->used)
            {
                continue;
            }

            switch (L->type)
            {
            case SWITCH_CB_EDGE:
            {
                /* Debounce: reject edges arriving within this listener's window. */
                uint32_t delta_ms = switch_tick_2_ms(self, now - L->cfg.edge.last_tick);
                if (delta_ms >= L->cfg.edge.debounce_ms)
                {
                    L->cfg.edge.last_tick = now;
                    if (L->cb.edge_cb != NULL)
                    {
                        L->cb.edge_cb(edge);
                    }
                }
                break;
            }

            case SWITCH_CB_PRESS:
            {
                /* Fire on the edge opposite to start_edge, once per press. */
                if (edge != L->cfg.press.start_edge)
                {
                    bool armed = (L->cfg.press.start_edge == SWITCH_EDGE_FALLING)
                                 ? priv->falling_armed : priv->rising_armed;
                    if (armed)
                    {
                        SWITCH_TICK_TYPE start = (L->cfg.press.start_edge == SWITCH_EDGE_FALLING)
                                                 ? priv->falling_tick : priv->rising_tick;
                        uint32_t dur_ms = switch_tick_2_ms(self, now - start);
                        if (dur_ms >= L->cfg.press.debounce_ms)
                        {
                            switch_press_type_t t;
                            if (classify_press(dur_ms, L->cfg.press.short_press_max_ms,
                                               L->cfg.press.long_press_min_ms, &t))
                            {
                                if (L->cb.press_cb != NULL)
                                {
                                    L->cb.press_cb(t);
                                }
                            }
                        }
                    }
                }
                break;
            }

            default:
                break;
            }
        }

        /* Disarm the direction that just completed, so the next same-polarity
         * edge does not re-trigger until a fresh start edge re-arms it. */
        if (edge == SWITCH_EDGE_FALLING)
        {
            priv->rising_armed = false;
        }
        else
        {
            priv->falling_armed = false;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                              ISR callbacks                                  */
/* -------------------------------------------------------------------------- */

void switch_handler_rising_callback(switch_handler_t *self)
{
    if (self == NULL || PRIV_DATA(self) == NULL || !PRIV_DATA(self)->if_init)
    {
        return;
    }
    switch_push_event(self, SWITCH_EDGE_RISING);
}

void switch_handler_falling_callback(switch_handler_t *self)
{
    if (self == NULL || PRIV_DATA(self) == NULL || !PRIV_DATA(self)->if_init)
    {
        return;
    }
    switch_push_event(self, SWITCH_EDGE_FALLING);
}

/* -------------------------------------------------------------------------- */
/*                              Public API                                     */
/* -------------------------------------------------------------------------- */

switch_handler_status_t switch_inst(switch_handler_t *self,
                                    const switch_os_interface_t *os_if)
{
    if (self == NULL || os_if == NULL)
    {
        return SWITCH_HANDLER_ERR_PARAM_INVALID;
    }
    if (os_if->pf_get_tick == NULL || os_if->pf_tick_2_ms == NULL ||
        os_if->pf_queue_create == NULL ||
        os_if->pf_queue_delete == NULL || os_if->pf_queue_send == NULL ||
        os_if->pf_queue_receive == NULL || os_if->pf_thread_create == NULL ||
        os_if->pf_delay_ms == NULL)
    {
        return SWITCH_HANDLER_ERR_PARAM_INVALID;
    }

    if (self->magic == SWITCH_HANDLER_MAGIC)
    {
        if (PRIV_DATA(self) != NULL && PRIV_DATA(self)->if_init)
        {
            return SWITCH_HANDLER_ERR_ALREADY_INITED;
        }
    }

    memset(self, 0, sizeof(*self));

    PRIV_DATA(self) = MALLOC(sizeof(switch_handler_priv_data_t));
    if (PRIV_DATA(self) == NULL)
    {
        return SWITCH_HANDLER_ERR_OTHERS;
    }
    memset(PRIV_DATA(self), 0, sizeof(*PRIV_DATA(self)));
    PRIV_DATA(self)->os_if = os_if;

    uint32_t queue_data_size = (uint32_t)sizeof(switch_priv_event_t);
    int32_t ret = os_if->pf_queue_create(SWITCH_HANDLER_QUEUE_DEPTH, queue_data_size,
                                          &PRIV_DATA(self)->queue_handle);
    if (ret != SWITCH_SUCCESS)
    {
        FREE(PRIV_DATA(self));
        PRIV_DATA(self) = NULL;
        return SWITCH_HANDLER_ERR_OTHERS;
    }

    void *thread_handle = NULL;
    ret = os_if->pf_thread_create(
        "SWITCH",
        switch_handler_thread,
        SWITCH_HANDLER_THREAD_STACK_DEPTH,
        SWITCH_HANDLER_THREAD_PRIORITY,
        &thread_handle,
        (void *)self);

    if (ret != SWITCH_SUCCESS || thread_handle == NULL)
    {
        os_if->pf_queue_delete(PRIV_DATA(self)->queue_handle);
        FREE(PRIV_DATA(self));
        PRIV_DATA(self) = NULL;
        return SWITCH_HANDLER_ERR_OTHERS;
    }

    PRIV_DATA(self)->thread_handle = thread_handle;
    PRIV_DATA(self)->if_init       = true;
    self->magic = SWITCH_HANDLER_MAGIC;

    return SWITCH_HANDLER_OK;
}

switch_handler_status_t switch_register(switch_handler_t *self,
                                        switch_cb_type_t cb_type,
                                        const switch_arg_u *arg,
                                        switch_listener_t **out_handle)
{
    if (self == NULL || arg == NULL || out_handle == NULL)
    {
        return SWITCH_HANDLER_ERR_PARAM_INVALID;
    }
    if (self->magic != SWITCH_HANDLER_MAGIC || PRIV_DATA(self) == NULL ||
        !PRIV_DATA(self)->if_init)
    {
        return SWITCH_HANDLER_ERR_NOT_INITED;
    }

    /* The callback for the requested mode must be non-NULL. */
    switch (cb_type)
    {
    case SWITCH_CB_EDGE:
        if (arg->edge.edge_cb == NULL)
        {
            return SWITCH_HANDLER_ERR_PARAM_INVALID;
        }
        break;
    case SWITCH_CB_PRESS:
        if (arg->press.press_cb == NULL)
        {
            return SWITCH_HANDLER_ERR_PARAM_INVALID;
        }
        break;
    default:
        return SWITCH_HANDLER_ERR_PARAM_INVALID;
    }

    switch_handler_priv_data_t *priv = PRIV_DATA(self);
    for (uint32_t i = 0; i < SWITCH_MAX_LISTENERS; i++)
    {
        switch_listener_t *L = &priv->listeners[i];
        if (!L->used)
        {
            L->used = true;
            L->type = cb_type;
            switch (cb_type)
            {
            case SWITCH_CB_EDGE:
                L->cb.edge_cb            = arg->edge.edge_cb;
                L->cfg.edge.debounce_ms  = arg->edge.debounce_ms;
                L->cfg.edge.last_tick    = 0;
                break;
            case SWITCH_CB_PRESS:
                L->cb.press_cb                       = arg->press.press_cb;
                L->cfg.press.start_edge              = arg->press.start_edge;
                L->cfg.press.debounce_ms             = arg->press.debounce_ms;
                L->cfg.press.short_press_max_ms      = arg->press.short_press_max_ms;
                L->cfg.press.long_press_min_ms       = arg->press.long_press_min_ms;
                break;
            default:
                break;
            }
            *out_handle = L;
            return SWITCH_HANDLER_OK;
        }
    }

    return SWITCH_HANDLER_ERR_FULL;
}

switch_handler_status_t switch_unregister(switch_handler_t *self,
                                          switch_listener_t *handle)
{
    if (self == NULL || handle == NULL)
    {
        return SWITCH_HANDLER_ERR_PARAM_INVALID;
    }
    if (self->magic != SWITCH_HANDLER_MAGIC || PRIV_DATA(self) == NULL ||
        !PRIV_DATA(self)->if_init)
    {
        return SWITCH_HANDLER_ERR_NOT_INITED;
    }

    switch_handler_priv_data_t *priv = PRIV_DATA(self);

    /* Validate the handle points inside this instance's listener table. */
    if (handle < &priv->listeners[0] ||
        handle >= &priv->listeners[SWITCH_MAX_LISTENERS])
    {
        return SWITCH_HANDLER_ERR_NOT_FOUND;
    }

    if (!handle->used)
    {
        return SWITCH_HANDLER_ERR_NOT_FOUND;
    }

    handle->used = false;
    return SWITCH_HANDLER_OK;
}
