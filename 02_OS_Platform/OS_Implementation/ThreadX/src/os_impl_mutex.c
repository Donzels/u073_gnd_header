#include "osal_internal_mutex.h"
#include "os_threadx.h"
#include "osal_internal_heap.h"

#if (OSAL_RTOS_SUPPORT == THREADX_SUPPORT)

int32_t os_mutex_create_impl(osal_mutex_handle_t *p_mutex_handle)
{
    int32_t ret;
    TX_MUTEX *cur_mutex_handle;
    
    cur_mutex_handle = (TX_MUTEX *)os_heap_malloc_impl(sizeof(TX_MUTEX));
    if (cur_mutex_handle == NULL)
    {
        ret = OSAL_ERROR;
    }
    else
    {
        UINT status = tx_mutex_create(cur_mutex_handle, "mutex", TX_NO_INHERIT);
        if (status != TX_SUCCESS)
        {
            os_heap_free_impl(cur_mutex_handle);
            ret = OSAL_ERROR;
        }
        else
        {
            *p_mutex_handle = (osal_mutex_handle_t)cur_mutex_handle;
            ret = OSAL_SUCCESS;
        }
    }
    return ret;
}

void os_mutex_delete_impl(osal_mutex_handle_t mutex_handle)
{
    TX_MUTEX *handle = (TX_MUTEX *)mutex_handle;
    if (handle != NULL)
    {
        tx_mutex_delete(handle);
        os_heap_free_impl(handle);
    }
}

int32_t os_mutex_give_impl(osal_mutex_handle_t mutex_handle)
{
    int32_t ret;
    UINT status;
    TX_MUTEX *handle = (TX_MUTEX *)mutex_handle;

    OSAL_CHECK_POINTER(handle);

    status = tx_mutex_put(handle);

    if (status == TX_SUCCESS)
    {
        ret = OSAL_SUCCESS;
    }
    else
    {
        ret = OSAL_ERROR;
    }
    return ret;
}

int32_t os_mutex_take_impl(osal_mutex_handle_t mutex_handle, osal_tick_type_t timeout)
{
    int32_t ret;
    UINT status;
    TX_MUTEX *handle = (TX_MUTEX *)mutex_handle;
    OSAL_CHECK_POINTER(handle);

    status = tx_mutex_get(handle, OS_MS_TO_TICKS(timeout));

    if (status == TX_SUCCESS)
    {
        ret = OSAL_SUCCESS;
    }
    else
    {
        ret = OSAL_ERROR;
    }
    return ret;
}

#endif // OSAL_RTOS_SUPPORT
