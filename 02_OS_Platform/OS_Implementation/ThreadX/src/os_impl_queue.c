#include "osal_internal_queue.h"
#include "os_threadx.h"
#include "osal_internal_heap.h"

#if (OSAL_RTOS_SUPPORT == THREADX_SUPPORT)

int32_t os_queue_create_impl( size_t queue_depth, size_t data_size, osal_queue_handle_t *p_queue_handle)
{
    int32_t ret;
    TX_QUEUE *cur_queue_handle;
    
    cur_queue_handle = (TX_QUEUE *)os_heap_malloc_impl(sizeof(TX_QUEUE));
    if (cur_queue_handle == NULL)
    {
        ret = OSAL_ERROR;
    }
    else
    {
        /* ThreadX queue memory requirement calculation
           Message size = number of ULONGs needed (must round up)
           Queue memory = queue_depth * message_size_in_ulongs * sizeof(ULONG) */
        ULONG message_size_ulongs = (data_size + sizeof(ULONG) - 1) / sizeof(ULONG);
        ULONG queue_size = queue_depth * message_size_ulongs * sizeof(ULONG);
        VOID *queue_memory = os_heap_malloc_impl(queue_size);
        
        if (queue_memory == NULL)
        {
            os_heap_free_impl(cur_queue_handle);
            ret = OSAL_ERROR;
        }
        else
        {
            UINT status = tx_queue_create(cur_queue_handle, "queue", message_size_ulongs, 
                                         (VOID *)queue_memory, queue_size);
            if (status != TX_SUCCESS)
            {
                os_heap_free_impl(queue_memory);
                os_heap_free_impl(cur_queue_handle);
                ret = OSAL_ERROR;
            }
            else
            {
                *p_queue_handle = (osal_queue_handle_t)cur_queue_handle;
                ret = OSAL_SUCCESS;
            }
        }
    }
    return ret;
}

void os_queue_delete_impl(osal_queue_handle_t queue_handle)
{
    TX_QUEUE *handle = (TX_QUEUE *)queue_handle;
    if (handle != NULL)
    {
        tx_queue_delete(handle);
        os_heap_free_impl(handle);
    }
}

int32_t os_queue_send_impl(osal_queue_handle_t queue_handle, const void *data, osal_tick_type_t timeout)
{
    int32_t ret;
    UINT status;
    TX_QUEUE *handle = (TX_QUEUE *)queue_handle;

    OSAL_CHECK_POINTER(queue_handle);
    OSAL_CHECK_POINTER(data);

    status = tx_queue_send(handle, (VOID *)data, OS_MS_TO_TICKS(timeout));

    if (status == TX_SUCCESS)
    {
        ret = OSAL_SUCCESS;
    }
    else if (status == TX_QUEUE_FULL)
    {
        ret = OSAL_ERROR;
    }
    else
    {
        ret = OSAL_ERROR;
    }
    return ret;
}

int32_t os_queue_receive_impl(osal_queue_handle_t queue_handle, void *data, osal_tick_type_t timeout)
{
    int32_t ret;
    UINT status;
    TX_QUEUE *handle = (TX_QUEUE *)queue_handle;
    OSAL_CHECK_POINTER(handle);
    OSAL_CHECK_POINTER(data);

    status = tx_queue_receive(handle, data, OS_MS_TO_TICKS(timeout));
    
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

int32_t os_queue_msg_waiting_impl(osal_queue_handle_t queue_handle)
{
    int32_t ret;
    TX_QUEUE *handle = (TX_QUEUE *)queue_handle;
    
    if (handle != NULL)
    {
        ret = handle->tx_queue_enqueued;
    }
    else
    {
        ret = 0;
    }
    return ret;
}

#endif // OSAL_RTOS_SUPPORT
