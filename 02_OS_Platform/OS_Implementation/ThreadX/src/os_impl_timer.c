#include "osal_internal_timer.h"
#include "osal_internal_globaldefs.h"
#include "os_threadx.h"
#include "osal_internal_heap.h"

#if (OSAL_RTOS_SUPPORT == THREADX_SUPPORT)

typedef struct {
    TX_TIMER *tx_timer;
    uint8_t auto_reload;
    osal_tick_type_t timer_period;  /* Store timer period for os_timer_period_get_impl */
} timer_wrapper_t;

static void os_timer_cb(ULONG arg)
{
    osal_timer_internal_record_t *timer_record = (osal_timer_internal_record_t *)arg;
    if (timer_record != NULL && timer_record->timer_id.func != NULL)
    {
        timer_record->timer_id.func(timer_record->timer_id.timer_handle, timer_record->timer_id.arg);
    }
}

static int32_t os_timer_restart_with_period(TX_TIMER *handle, uint32_t ticks, uint8_t auto_reload)
{
    UINT status;
    
    status = tx_timer_deactivate(handle);
    if (status != TX_SUCCESS)
    {
        return OSAL_ERROR;
    }
    
    /* Set reschedule_ticks based on auto_reload flag */
    UINT reschedule_ticks = auto_reload ? ticks : 0;
    status = tx_timer_change(handle, ticks, reschedule_ticks);
    if (status != TX_SUCCESS)
    {
        return OSAL_ERROR;
    }
    
    status = tx_timer_activate(handle);
    if (status != TX_SUCCESS)
    {
        return OSAL_ERROR;
    }
    
    return OSAL_SUCCESS;
}

int32_t os_timer_create_impl(osal_timer_handle_t *p_timer_handle, osal_timer_internal_record_t *timer_record)
{
    int32_t ret = OSAL_SUCCESS;
    
    /* Allocate wrapper structure */
    timer_wrapper_t *wrapper = (timer_wrapper_t *)os_heap_malloc_impl(sizeof(timer_wrapper_t));
    if (wrapper == NULL)
    {
        return OSAL_INVALID_POINTER;
    }
    
    /* Allocate TX_TIMER */
    wrapper->tx_timer = (TX_TIMER *)os_heap_malloc_impl(sizeof(TX_TIMER));
    if (wrapper->tx_timer == NULL)
    {
        os_heap_free_impl(wrapper);
        return OSAL_INVALID_POINTER;
    }
    
    /* Store auto_reload flag in wrapper */
    wrapper->auto_reload = timer_record->auto_reload;
    wrapper->timer_period = timer_record->timer_period;
    
    /* Set timer_handle to wrapper */
    timer_record->timer_id.timer_handle = (osal_timer_handle_t)wrapper;

    UINT auto_activate = timer_record->auto_reload ? TX_AUTO_ACTIVATE : TX_NO_ACTIVATE;
    UINT reschedule_ticks = timer_record->auto_reload ? timer_record->timer_period : 0;
    UINT status = tx_timer_create(wrapper->tx_timer, 
                                  (CHAR *)timer_record->timer_name,
                                  os_timer_cb, 
                                  (ULONG)timer_record,
                                  timer_record->timer_period,
                                  reschedule_ticks,
                                  auto_activate);
    
    if (status != TX_SUCCESS)
    {
        os_heap_free_impl(wrapper->tx_timer);
        os_heap_free_impl(wrapper);
        ret = OSAL_INVALID_POINTER;
    }
    else
    {
        *p_timer_handle = (osal_timer_handle_t)wrapper;
    }
    return ret;
}

int32_t os_timer_start_impl(osal_timer_handle_t timer_handle, osal_tick_type_t ticks_to_wait)
{
    timer_wrapper_t *wrapper = (timer_wrapper_t *)timer_handle;
    if (wrapper == NULL || wrapper->tx_timer == NULL)
    {
        return OSAL_ERROR;
    }
    
    /* Validate ticks_to_wait is not zero to avoid TX_ACTIVATE_ERROR */
    if (ticks_to_wait == 0)
    {
        return OSAL_ERROR;
    }
    
    /* Ensure timer is stopped before starting to avoid issues with already active timer */
    return os_timer_restart_with_period(wrapper->tx_timer, OS_MS_TO_TICKS(ticks_to_wait), wrapper->auto_reload);
}

int32_t os_timer_stop_impl(osal_timer_handle_t timer_handle, osal_tick_type_t ticks_to_wait)
{
    int32_t ret = OSAL_SUCCESS;
    UINT status;

    timer_wrapper_t *wrapper = (timer_wrapper_t *)timer_handle;
    if (wrapper == NULL || wrapper->tx_timer == NULL)
    {
        return OSAL_ERROR;
    }

    status = tx_timer_deactivate(wrapper->tx_timer);
    
    if (status != TX_SUCCESS)
    {
        ret = OSAL_ERROR;
    }
    return ret;
}

int32_t os_timer_period_change_impl(osal_timer_handle_t timer_handle, osal_tick_type_t new_period, osal_tick_type_t ticks_to_wait)
{
    int32_t ret = OSAL_SUCCESS;
    UINT status;

    timer_wrapper_t *wrapper = (timer_wrapper_t *)timer_handle;
    if (wrapper == NULL || wrapper->tx_timer == NULL)
    {
        return OSAL_ERROR;
    }

    // For periodic timer, both should be new_period; for one-shot, reschedule_ticks = 0
    UINT reschedule_ticks = wrapper->auto_reload ? OS_MS_TO_TICKS(new_period) : 0;
    status = tx_timer_change(wrapper->tx_timer, OS_MS_TO_TICKS(new_period), reschedule_ticks);
    
    if (status != TX_SUCCESS)
    {
        ret = OSAL_ERROR;
    }
    return ret;
}

int32_t os_timer_delete_impl(osal_timer_handle_t timer_handle, osal_tick_type_t ticks_to_wait)
{
    int32_t ret = OSAL_SUCCESS;
    UINT status;

    timer_wrapper_t *wrapper = (timer_wrapper_t *)timer_handle;
    if (wrapper == NULL || wrapper->tx_timer == NULL)
    {
        return OSAL_ERROR;
    }

    status = tx_timer_delete(wrapper->tx_timer);
    
    if (status != TX_SUCCESS)
    {
        ret = OSAL_ERROR;
    }
    else
    {
        os_heap_free_impl(wrapper->tx_timer);
        os_heap_free_impl(wrapper);
    }
    return ret;
}

int32_t os_timer_reset_impl(osal_timer_handle_t timer_handle, osal_tick_type_t ticks_to_wait)
{
    timer_wrapper_t *wrapper = (timer_wrapper_t *)timer_handle;
    if (wrapper == NULL || wrapper->tx_timer == NULL)
    {
        return OSAL_ERROR;
    }

    // Deactivate and reactivate to reset
    return os_timer_restart_with_period(wrapper->tx_timer, OS_MS_TO_TICKS(ticks_to_wait), wrapper->auto_reload);
}

osal_tick_type_t os_timer_period_get_impl(osal_timer_handle_t timer_handle)
{
    timer_wrapper_t *wrapper = (timer_wrapper_t *)timer_handle;
    if (wrapper == NULL)
    {
        return 0;
    }
    
    return wrapper->timer_period;
}

#endif // OSAL_RTOS_SUPPORT
