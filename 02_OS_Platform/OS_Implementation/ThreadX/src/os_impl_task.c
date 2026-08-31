#include "osal_internal_task.h"
#include "os_threadx.h"
#include "osal_internal_heap.h"
#include <string.h>

#if (OSAL_RTOS_SUPPORT == THREADX_SUPPORT)

#define OSAL_MAX_NAME_LEN 16
#define OSAL_CHECK_APINAME(str) OSAL_CHECK_STRING(str, OSAL_MAX_NAME_LEN, OSAL_ERR_NAME_TOO_LONG)

// ThreadX task wrapper to adapt function signature
typedef struct {
    void (*original_func)(void *);
    void *original_arg;
} task_wrapper_arg_t;

typedef struct {
    TX_THREAD thread;
    osal_stackptr_t stack_pointer;
    size_t stack_size;
    uint8_t stack_allocated;
    task_wrapper_arg_t wrapper;
} osal_threadx_task_handle_t;

static void task_entry_wrapper(ULONG arg)
{
    task_wrapper_arg_t *wrapper = (task_wrapper_arg_t *)arg;
    if (wrapper != NULL && wrapper->original_func != NULL)
    {
        wrapper->original_func(wrapper->original_arg);
    }
}

int32_t os_task_create_impl(osal_task_internal_record_t *p_task)
{
    int32_t ret = OSAL_SUCCESS;
    OSAL_CHECK_APINAME(p_task->task_name);

    osal_threadx_task_handle_t *handle = (osal_threadx_task_handle_t *)os_heap_malloc_impl(sizeof(osal_threadx_task_handle_t));
    if (handle == NULL)
    {
        return OSAL_ERROR;
    }
    memset(handle, 0, sizeof(osal_threadx_task_handle_t));

    /* Allocate stack memory */
    if (p_task->stack_pointer == NULL)
    {
        p_task->stack_pointer = (osal_stackptr_t)os_heap_malloc_impl(p_task->stack_size);
        if (p_task->stack_pointer == NULL)
        {
            os_heap_free_impl(handle);
            return OSAL_ERROR;
        }
        handle->stack_allocated = 1U;
    }

    handle->stack_pointer = p_task->stack_pointer;
    handle->stack_size = p_task->stack_size;

    handle->wrapper.original_func = p_task->entry_function_pointer;
    handle->wrapper.original_arg = p_task->entry_arg;

    UINT status = tx_thread_create(&handle->thread, (CHAR *)p_task->task_name,
                                   task_entry_wrapper, (ULONG)&handle->wrapper,
                                   handle->stack_pointer,  /* Stack pointer */
                                   handle->stack_size,     /* Stack size */
                                   p_task->priority,
                                   p_task->priority,
                                   TX_NO_TIME_SLICE,
                                   TX_AUTO_START);
    
    if (status != TX_SUCCESS)
    {
        if (handle->stack_allocated != 0U)
        {
            os_heap_free_impl(handle->stack_pointer);
        }
        os_heap_free_impl(handle);
        ret = OSAL_ERROR;
    }
    else
    {
        if(p_task->p_task_handle)
        {
            *(p_task->p_task_handle) = (osal_task_handle_t)handle;
        }
        ret = OSAL_SUCCESS;
    }
    return ret;
}

void os_task_delete_impl(osal_task_handle_t task_handle)
{
    osal_threadx_task_handle_t *handle = (osal_threadx_task_handle_t *)task_handle;
    if (handle != NULL)
    {
        tx_thread_delete(&handle->thread);
        if (handle->stack_allocated != 0U)
        {
            os_heap_free_impl(handle->stack_pointer);
            handle->stack_allocated = 0U;
        }
        os_heap_free_impl(handle);
    }
}

void os_task_start_impl(void)
{
    // ThreadX scheduler is started with tx_kernel_enter()
    // This should be called from main, not from here
    tx_kernel_enter();
}

void os_task_suspend_impl(osal_task_handle_t task_handle)
{
    osal_threadx_task_handle_t *handle = (osal_threadx_task_handle_t *)task_handle;
    if (handle != NULL)
    {
        tx_thread_suspend(&handle->thread);
    }
}

void os_task_suspend_all_impl(void)
{
    // ThreadX does not have a direct equivalent to suspend all tasks
    // Use critical section to disable task switching
    // Note: This is a simplified implementation
    // In ThreadX, typically use mutex or semaphore for synchronization instead
}

void os_task_resume_impl(osal_task_handle_t task_handle)
{
    osal_threadx_task_handle_t *handle = (osal_threadx_task_handle_t *)task_handle;
    if (handle != NULL)
    {
        tx_thread_resume(&handle->thread);
    }
}

void os_task_delay_impl(uint32_t ticks)
{
    tx_thread_sleep(ticks);
}

void os_task_delay_ms_impl(uint32_t ms)
{
    tx_thread_sleep(OS_MS_TO_TICKS(ms));
}

uint32_t os_enter_critical_impl(void)
{
    return tx_interrupt_control(TX_INT_DISABLE);
}

void os_exit_critical_impl(uint32_t primask)
{
    tx_interrupt_control(primask);
}

int32_t os_port_yield_impl(void)
{
    int32_t ret;
    tx_thread_relinquish();
    ret = OSAL_SUCCESS;
    return ret;
}

void os_task_disable_interrupts_impl()
{
    tx_interrupt_control(TX_INT_DISABLE);
}

void os_task_enable_interrupts_impl()
{
    tx_interrupt_control(TX_INT_ENABLE);
}

#if (configCHECK_FOR_STACK_OVERFLOW > 0) && USE_OSAL
void tx_application_define(void *first_unused_memory)
{
    // Application initialization for ThreadX
}
#endif // configCHECK_FOR_STACK_OVERFLOW

osal_tick_type_t os_task_get_tick_count_impl(void)
{
    osal_tick_type_t os_ticks = tx_time_get();
    return os_ticks;
}

#endif // OSAL_RTOS_SUPPORT
