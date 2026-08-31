/**
 * @file rfid_handler.h
 * @brief RFID Decoder Handler (Input Capture + Demodulation)
 * @author tzl
 * @version 1.0.0
 * @date 2025-12-22
 *
 * Basic APIs for RFID data decoding via timer input capture.
 * Supports edge capture, Manchester code demodulation, and parity check.
 *
 * All RTOS dependencies (thread, semaphore) are injected at init time through
 * rfid_os_interface_t — the core code never calls CMSIS-OS2 / FreeRTOS
 * directly, so it ports across RTOSes unchanged.
 */

#ifndef __RFID_HANDLER_H__
#define __RFID_HANDLER_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Precision control: 0=basic, 1=high precision */
#define RFID_MORE_PRECISE                   1

/* Decoded tag length (bytes) handed to the decode-complete callback. */
#define RFID_DATA_LEN                       5

/*
 * Read-failed sentinel. The handler fills the output buffer with this when no
 * tag is decoded (demod timeout or failure). Every byte is 0xFF. The reader
 * compares the result against this to decide whether to retry.
 */
#define RFID_FAIL_CODE              { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

/* RTOS config */
#define RFID_HANDLER_THREAD_PRIORITY        24  /* Default thread priority (platform-specific value) */
#define RFID_HANDLER_THREAD_STACK_DEPTH     1024

/* "Wait forever" timeout passed to pf_os_sema_take(). */
#define RFID_OS_WAIT_FOREVER                UINT32_MAX

/* RTT debug */
#include "dbg_console.h"
/* #include "SEGGER_RTT.h"                                                       */
/* extern int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...); */
/* #define RFID_DEBUG_OUT(fmt, ...)  SEGGER_RTT_printf(0, fmt, ##__VA_ARGS__)    */
#define RFID_DEBUG_OUT(fmt, ...)      dbg_console_printf(fmt, ##__VA_ARGS__)     /* Output log to debug console */

/* Time threshold (1us granularity) */
/* standard half period = 256us, full period = 512us(1/125k * 64) */
#define HALF_PERIOD_HIGH    384  /* 0.5T upper limit (us) */
#define HALF_PERIOD_LOW     128  /* 0.5T lower limit (us) */
#define FULL_PERIOD_HIGH    640  /* 1T upper limit (us) */
#define FULL_PERIOD_LOW     384  /* 1T lower limit (us) */

/**
 * @def RFID_HANDLER_MAGIC
 * @brief Magic marker stamped on a fully-initialized rfid_handler_t instance.
 *
 * Lets rfid_handler_inst() distinguish a live instance from one that is still
 * zero-initialised or holds a stale wild pointer in rfid_priv_data, without
 * ever dereferencing that (possibly invalid) private-data pointer.
 */
#define RFID_HANDLER_MAGIC              0x52464944U  /* "RFID" */

/* Status codes */
typedef enum
{
    RFID_HANDLER_OK = 0,                /* Success */
    RFID_HANDLER_ERR_PARAM_INVALID,     /* Invalid parameter */
    RFID_HANDLER_ERR_ALREADY_INITED,    /* Handler has already been initialized */
    RFID_HANDLER_ERR_HANDLER_NOT_READY, /* Not initialized */
    RFID_HANDLER_ERR_OTHERS             /* Other errors */
} rfid_handler_status_t;

/* Capture edge type */
typedef enum
{
    EDGE_RISING = 0,
    EDGE_FALLING,
    EDGE_ERR
} capture_edge_t;

/* RFID hardware ops */
typedef struct
{
    /**
     * @brief Init RFID hardware (timer input capture)
     * @note 1. Input capture time precision: 1 microsecond (us)
     * @note 2. Timer auto-reload register (ARR) value must be > HALF_PERIOD_HIGH (384us)
     *          to avoid counter overflow during capture
     */
    void (*pf_rfid_init)(void);
    void (*pf_rfid_open)(void);   /* Enable RFID capture */
    void (*pf_rfid_close)(void);  /* Disable RFID capture */
} rfid_ops_t;

/* Input capture ops */
typedef struct
{
    capture_edge_t (*pf_get_capture_edge)(void); /* Get current capture edge */
#if(!RFID_MORE_PRECISE)
    uint32_t (*pf_get_counter)(void);            /* Get counter value */
    void (*pf_clear_counter)(void);              /* Clear counter */
#else
    uint32_t (*pf_get_ic_value)(capture_edge_t capture_edge); /* Get IC value */
    uint32_t (*pf_get_arr)(void);               /* Get Auto Reload Register value */
#endif

}io_capture_ops_t;

/* Decode complete callback */
typedef struct
{
    void *arg;                                  /* Callback param */
    void (*pf_rfid_cpl_cb)(uint8_t *rfid, void *arg); /* Decode complete CB */
} rfid_cpl_cb_ctx_t;

/* -------------------------------------------------------------------------- */
/*                          OS-abstraction interface                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief OS/hardware abstraction for the RFID handler.
 *
 * Every RTOS dependency the handler needs is a function pointer here. The
 * application populates one of these with the actual OSAL / CMSIS-OS2 calls and
 * passes it to rfid_handler_inst(). The handler core never touches the RTOS
 * directly.
 *
 * All operations return 0 on success, non-zero on failure — matching the OSAL
 * convention used by hall_os_interface_t / led_os_interface_t.
 */
typedef struct
{
    /* Thread: create returns 0 on success; handle returned via out-param. */
    int32_t (*pf_os_thread_create)(const char *name,
                                   void (*entry)(void *),
                                   size_t stack_size,
                                   uint32_t priority,
                                   void **p_handle,
                                   void *arg);

    /* Semaphore: create returns 0 on success; handle returned via out-param. */
    int32_t (*pf_os_sema_create)(void **p_sema_handle, uint32_t max_count, uint32_t init_count);
    void    (*pf_os_sema_delete)(void *sema_handle);

    /* Semaphore give (release). ISR-safe. Returns 0 on success. */
    int32_t (*pf_os_sema_give)(void *sema_handle);

    /* Semaphore take (acquire) with timeout. Returns 0 on success, non-zero on timeout. */
    int32_t (*pf_os_sema_take)(void *sema_handle, uint32_t timeout);

    /* Queue: create returns 0 on success; handle returned via out-param. */
    int32_t (*pf_os_queue_create)(size_t queue_depth, size_t data_size, void **p_queue_handle);
    void    (*pf_os_queue_delete)(void *queue_handle);
    /* Queue send/receive with timeout. Returns 0 on success, non-zero on failure/timeout. */
    int32_t (*pf_os_queue_send)(void *queue_handle, const void *data, uint32_t timeout);
    int32_t (*pf_os_queue_receive)(void *queue_handle, void *data, uint32_t timeout);
} rfid_os_interface_t;

/* Handler input args */
typedef struct
{
    rfid_ops_t          *rfid_ops;       /* RFID hardware ops */
    io_capture_ops_t    *io_capture_ops; /* Input capture ops */
    rfid_os_interface_t *os_if;          /* OS-abstraction interface (injected) */
}rfid_handler_input_arg_t;

/* Private data forward decl */
typedef struct rfid_priv_data rfid_priv_data_t;

/* RFID handler struct */
typedef struct
{
    uint32_t                    magic;           /* Set to RFID_HANDLER_MAGIC only after full init succeeds */
    rfid_priv_data_t            *rfid_priv_data; /* Private data */
    rfid_handler_input_arg_t    *input_arg;      /* Input args */
} rfid_handler_t;

/* Public APIs */
rfid_handler_status_t rfid_handler_inst(rfid_handler_t *const self, rfid_handler_input_arg_t *const input_arg);
/*
 * Start an asynchronous RFID read.
 *
 * The handler thread performs up to max_attempts capture/decode cycles. A cycle
 * that yields RFID_FAIL_CODE (all 0xFF) is considered a failure and retried, until
 * either a valid tag is decoded or max_attempts is exhausted. The completion
 * callback is invoked exactly once, with the decoded tag or — if every attempt
 * failed — RFID_FAIL_CODE.
 *
 * @param self           Initialised handler instance.
 * @param rfid_cpl_cb_ctx  Completion callback + user arg, invoked once when the
 *                       whole read (including retries) finishes.
 * @param max_attempts  Maximum number of capture attempts (>= 1). A read that
 *                       keeps failing will be tried this many times before the
 *                       callback receives the failure result.
 */
rfid_handler_status_t rfid_read_asy(rfid_handler_t *const self,
                                    rfid_cpl_cb_ctx_t *rfid_cpl_cb_ctx,
                                    uint8_t max_attempts);
void demo_algo_isr_cb(rfid_handler_t *const self); /* ISR callback */

#endif //__RFID_HANDLER_H__
