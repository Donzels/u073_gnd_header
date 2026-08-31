#include "osal_internal_sema.h"
#include "os_threadx.h"
#include "osal_internal_heap.h"

#if (OSAL_RTOS_SUPPORT == THREADX_SUPPORT)

/*
 * Semaphore wrapper structure
 * Both binary and counting semaphores use TX_SEMAPHORE
 * Binary semaphore: max_count = 1, initial_count = 0
 * Counting semaphore: max_count = user specified, initial_count = user specified
 */
typedef struct 
{
    TX_SEMAPHORE semaphore;
    uint32_t max_count;  // Max count: 1 for binary, custom for counting
} os_sema_wrapper_t;

int32_t os_sema_binary_create_impl(osal_sema_handle_t *p_sema_handle)
{
    int32_t ret;
    os_sema_wrapper_t *wrapper;
    
    wrapper = (os_sema_wrapper_t *)os_heap_malloc_impl(sizeof(os_sema_wrapper_t));
    if (wrapper == NULL)
    {
        ret = OSAL_INVALID_POINTER;
    }
    else
    {
        // Binary semaphore: initial count = 0 (unavailable), max_count = 1
        UINT status = tx_semaphore_create(&wrapper->semaphore, "binary_sema", 0);
        if (status != TX_SUCCESS)
        {
            os_heap_free_impl(wrapper);
            ret = OSAL_ERROR;
        }
        else
        {
            wrapper->max_count = 1;  // Binary semaphore max is 1
            *p_sema_handle = (osal_sema_handle_t)wrapper;
            ret = OSAL_SUCCESS;
        }
    }
    return ret;
}

int32_t os_sema_countings_create_impl(osal_sema_handle_t *p_sema_handle, uint32_t max_count, uint32_t init_count)
{
    int32_t ret;
    os_sema_wrapper_t *wrapper;
    
    wrapper = (os_sema_wrapper_t *)os_heap_malloc_impl(sizeof(os_sema_wrapper_t));
    if (wrapper == NULL)
    {
        ret = OSAL_ERROR;
    }
    else
    {
        // Counting semaphore with initial count and max_count
        UINT status = tx_semaphore_create(&wrapper->semaphore, "counting_sema", init_count);
        if (status != TX_SUCCESS)
        {
            os_heap_free_impl(wrapper);
            ret = OSAL_ERROR;
        }
        else
        {
            wrapper->max_count = max_count;  // Store max_count for checking on give
            *p_sema_handle = (osal_sema_handle_t)wrapper;
            ret = OSAL_SUCCESS;
        }
    }
    return ret;
}

void os_sema_delete_impl(osal_sema_handle_t sema_handle)
{
    os_sema_wrapper_t *wrapper = (os_sema_wrapper_t *)sema_handle;
    if (wrapper != NULL)
    {
        tx_semaphore_delete(&wrapper->semaphore);
        os_heap_free_impl(wrapper);
    }
}

int32_t os_sema_give_impl(osal_sema_handle_t sema_handle)
{
    int32_t ret;
    UINT status;
    os_sema_wrapper_t *wrapper = (os_sema_wrapper_t *)sema_handle;
    ULONG current_count;
    TX_THREAD *first_suspended;
    ULONG suspended_count;
    TX_SEMAPHORE *next_semaphore;

    OSAL_CHECK_POINTER(wrapper);

    // Check if semaphore count reaches max_count
    status = tx_semaphore_info_get(&wrapper->semaphore, NULL, &current_count,
                                    &first_suspended, &suspended_count, &next_semaphore);
    
    if (status == TX_SUCCESS && current_count >= wrapper->max_count)
    {
        // Reached max_count (1 for binary, custom for counting), cannot increment
        ret = OSAL_ERROR;
    }
    else
    {
        // Increment count (within limit)
        status = tx_semaphore_put(&wrapper->semaphore);
        ret = (status == TX_SUCCESS) ? OSAL_SUCCESS : OSAL_ERROR;
    }

    return ret;
}

int32_t os_sema_take_impl(osal_sema_handle_t sema_handle, osal_tick_type_t timeout)
{
    int32_t ret;
    UINT status;
    os_sema_wrapper_t *wrapper = (os_sema_wrapper_t *)sema_handle;
    OSAL_CHECK_POINTER(wrapper);

    // Decrement semaphore count (works for both binary and counting)
    status = tx_semaphore_get(&wrapper->semaphore, OS_MS_TO_TICKS(timeout));

    ret = (status == TX_SUCCESS) ? OSAL_SUCCESS : OSAL_ERROR;
    return ret;
}

#endif // OSAL_RTOS_SUPPORT
