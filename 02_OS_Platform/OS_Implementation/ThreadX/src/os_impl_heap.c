#include "osal_internal_heap.h"
#include "os_threadx.h"

#if (OSAL_RTOS_SUPPORT == THREADX_SUPPORT)

// ThreadX byte pool for dynamic memory allocation
static TX_BYTE_POOL os_byte_pool;

static uint8_t os_byte_pool_memory[OSAL_HEAP_POOL_SIZE]; 
static bool os_heap_initialized = false;

static void os_heap_init(void)
{
    tx_byte_pool_create(&os_byte_pool, "os_byte_pool", os_byte_pool_memory, OSAL_HEAP_POOL_SIZE);
}

void *os_heap_malloc_impl(size_t wanted_size)
{
    void *ptr;
    if (!os_heap_initialized)
    {
        os_heap_init();
        os_heap_initialized = true;
    }
    if (tx_byte_allocate(&os_byte_pool, &ptr, wanted_size, TX_NO_WAIT) == TX_SUCCESS)
    {
        return ptr;
    }
    return NULL;
}

void os_heap_free_impl(void *ptr)
{
    if (ptr != NULL)
    {
        tx_byte_release(ptr);
    }
}

#endif // OSAL_RTOS_SUPPORT
