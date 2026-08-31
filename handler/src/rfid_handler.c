/**
 * @file rfid_handler.c
 * @brief RFID Decoder Implementation
 * @author tzl
 * @version 1.0.0
 * @date 2025-12-22
 *
 * Manchester code demodulation, parity check, RTOS-based async decoding.
 *
 * Platform-agnostic: every OS dependency (thread, semaphore) comes in through
 * rfid_os_interface_t. The core never calls CMSIS-OS2 / FreeRTOS directly, so
 * the same code runs on any RTOS — only the injected table changes.
 */

#include "rfid_handler.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define MALLOC(size)        malloc(size)
#define FREE(ptr)           free(ptr)

/* Shortcut macros */
#define PRIV_DATA(self)     (self)->rfid_priv_data
#define INPUT_ARG(self)     (self)->input_arg
#define RFID_OPS(self)      INPUT_ARG(self)->rfid_ops
#define IO_CAPTURE(self)    INPUT_ARG(self)->io_capture_ops
#define OS_IF(self)         INPUT_ARG(self)->os_if
#define DEMOD_DATA(self)    (PRIV_DATA(self)->rfid_demod)

/* Data length defines */
#define DATA_256_BITS       256
#define DATA_128_BITS       128
#define DATA_64_BITS        64
#define HEADER_ORI_BITS     18   /* Manchester original header bits (9) */
#define HEADER_EXTEN_BITS   20   /* Manchester extension header bits (9+1) */

/* Manchester code definition:
   - "1" = 01 (LSB=1, MSB=0)
   - "0" = 10 (LSB=0, MSB=1)
   Original header: 9 consecutive Manchester "1" bits */
/* Extended header used for demodulation: adds a Manchester "0" (stop bit) before original header to improve capture probability */
#define ORI_HEADER_MANCHER_BITS     (0x15555ULL)
#define EXTEN_HEADER_MANCHER_BITS   (0x55556ULL)

#define HEADER_MANCHER_VALUE        (0x1FFULL)/* 9 consecutive 1 */

/* Full data period = 32.768ms (64��512us), demod timeout �� 2��period */
#define DEMOD_TOTAL_MS              120//400 /* Demodulation timeout (ms) */


/* Wave type */
typedef enum
{
    WAVE_0P5T = 0,  /* 0.5 period */
    WAVE_1T = 1,    /* 1 period */
    WAVE_INVALID = -1 /* Invalid wave */
} rfid_wave_t;

/* Decode status */
typedef enum
{
    DECODE_OK = 0,
    DECODE_IN_PROGRESS = -1,
    DECODE_TIMEOUT = -2,
    DECODE_FAIL = -3,
    DECODE_ERR_OTHER = -4
} rfid_decode_status_t;

/* 256-bit data storage */
typedef struct __attribute__((aligned(8)))
{
    uint64_t data0;
    uint64_t data1;
    uint64_t data2;
    uint64_t data3;
} uint256_t;

/* 128-bit data storage */
typedef struct __attribute__((aligned(8)))
{
    uint64_t low;
    uint64_t high;
} uint128_t;

/* RFID demodulation context */
typedef struct
{
    uint32_t timer_id;
    volatile uint32_t data256_idx;    /* Current index in 256-bit buffer */
    volatile int decode_state;        /* Decode status */
    volatile uint256_t data256;       /* Raw captured data */
} rfid_demod_t;

/* Bundled start request: max_attempts + callback context travel together through
 * the start queue so concurrent rfid_read_asy() calls never clobber each other. */
typedef struct
{
    uint8_t           max_attempts;
    rfid_cpl_cb_ctx_t *cb_ctx;
} rfid_start_req_t;

/* Private handler data */
typedef struct rfid_priv_data
{
    bool                    is_initialized;
    rfid_os_interface_t     *os_if;            /* Injected OS interface */
    void                    *thread_handle;    /* Processing thread handle */
    void                    *decode_complete_sem; /* Sem: decode complete */
    void                    *start_queue;      /* Queue: carries rfid_start_req_t to the handler thread */
#if(RFID_MORE_PRECISE)
    uint32_t                pre_ic_value;
#endif
    rfid_demod_t            rfid_demod;        /* Demodulation data */
} rfid_priv_data_t;

/* RFID data structure (header + data + parity) */
typedef union __attribute__((aligned(8)))
{
    struct
    {
        uint64_t header : 9;  /* 9-bit header */
        uint64_t d0 : 4;      /* Data 0 */
        uint64_t p0 : 1;      /* Parity 0 */
        uint64_t d1 : 4;      /* Data 1 */
        uint64_t p1 : 1;      /* Parity 1 */
        uint64_t d2 : 4;      /* Data 2 */
        uint64_t p2 : 1;      /* Parity 2 */
        uint64_t d3 : 4;      /* Data 3 */
        uint64_t p3 : 1;      /* Parity 3 */
        uint64_t d4 : 4;      /* Data 4 */
        uint64_t p4 : 1;      /* Parity 4 */
        uint64_t d5 : 4;      /* Data 5 */
        uint64_t p5 : 1;      /* Parity 5 */
        uint64_t d6 : 4;      /* Data 6 */
        uint64_t p6 : 1;      /* Parity 6 */
        uint64_t d7 : 4;      /* Data 7 */
        uint64_t p7 : 1;      /* Parity 7 */
        uint64_t d8 : 4;      /* Data 8 */
        uint64_t p8 : 1;      /* Parity 8 */
        uint64_t d9 : 4;      /* Data 9 */
        uint64_t p9 : 1;      /* Parity 9 */
        uint64_t pc0 : 1;     /* Column parity 0 */
        uint64_t pc1 : 1;     /* Column parity 1 */
        uint64_t pc2 : 1;     /* Column parity 2 */
        uint64_t pc3 : 1;     /* Column parity 3 */
        uint64_t stop_bit : 1;/* Stop bit */
    } field;
    uint64_t raw;
} rfid_data_packet_t;

/**
 * @brief Judge wave type by edge interval
 * @param interval: Edge interval (us)
 * @return Wave type (0.5T/1T/invalid)
 */
static rfid_wave_t judge_wave_type(uint32_t interval)
{
    if (interval < HALF_PERIOD_HIGH && interval >= HALF_PERIOD_LOW)
    {
        return WAVE_0P5T;
    }
    else if (interval < FULL_PERIOD_HIGH && interval >= FULL_PERIOD_LOW)
    {
        return WAVE_1T;
    }
    return WAVE_INVALID;
}

/**
 * @brief Swap bits to correct RFID data format
 * @param value: Raw data
 * @return Corrected data
 */
static uint8_t swap_data_bits(uint8_t value)
{
    uint8_t corrected = 0;
    /* Swap bit 0<->3, 1<->2 */
    corrected |= ((value & 0x08) >> 3) | ((value & 0x01) << 3);
    corrected |= ((value & 0x04) >> 1) | ((value & 0x02) << 1);
    /* Swap bit 4<->7, 5<->6 */
    corrected |= ((value & 0x80) >> 3) | ((value & 0x10) << 3);
    corrected |= ((value & 0x40) >> 1) | ((value & 0x20) << 1);
    return corrected;
}

/**
 * @brief Check RFID data parity (row + column)
 * @param packet: RFID data packet
 * @return true=valid, false=invalid
 */
static bool if_rfid_valid(rfid_data_packet_t packet)
{
    /* Check header */
    if (packet.field.header != HEADER_MANCHER_VALUE)
    {
        return false;
    }

    /* Row parity check */
    uint8_t row_parity[10] = {packet.field.p0, packet.field.p1, packet.field.p2, packet.field.p3, packet.field.p4,
                              packet.field.p5, packet.field.p6, packet.field.p7, packet.field.p8, packet.field.p9};
    uint8_t data[10] = {packet.field.d0, packet.field.d1, packet.field.d2, packet.field.d3, packet.field.d4,
                        packet.field.d5, packet.field.d6, packet.field.d7, packet.field.d8, packet.field.d9};

    for (uint8_t i = 0; i < 10; i++)
    {
        uint8_t parity = 0;
        for (uint8_t j = 0; j < 4; j++)
        {
            parity ^= (data[i] >> j) & 0x1;
        }
        if (row_parity[i] != parity)
        {
            return false;
        }
    }

    /* Column parity check */
    uint8_t col_parity[4] = {packet.field.pc0, packet.field.pc1, packet.field.pc2, packet.field.pc3};
    for (uint8_t i = 0; i < 4; i++)
    {
        uint8_t parity = 0;
        for (uint8_t j = 0; j < 10; j++)
        {
            parity ^= (data[j] >> i) & 0x1;
        }
        if (col_parity[i] != parity)
        {
            return false;
        }
    }

    /* Check stop bit (must be 0) */
    if (packet.field.stop_bit != 0)
    {
        return false;
    }

    return true;
}

/**
 * @brief Demodulate raw capture data to RFID bytes
 * @param self: RFID handler
 * @param output: Decoded RFID data (5 bytes)
 * @return Decode status
 */
static rfid_decode_status_t demodulate_rfid_data(rfid_handler_t *const self, uint8_t *output)
{
    /* Null check */
    if (!output)
    {
        return DECODE_ERR_OTHER;
    }

    rfid_demod_t *demod = &DEMOD_DATA(self);
    if (demod->decode_state != DECODE_OK)
    {
        return DECODE_IN_PROGRESS;
    }

    rfid_data_packet_t data_packet = {.raw = 0};
    const uint64_t header_pattern = EXTEN_HEADER_MANCHER_BITS; /* Manchester header pattern */
    uint8_t header_match_cnt = 0;
    uint64_t raw_data = 0;
    uint8_t bit_idx = 0;

    /* Search for header in 256-bit buffer */
    for (uint32_t buf_idx = 0; buf_idx < DATA_256_BITS; buf_idx++)
    {
        /* Get current 64-bit chunk */
        if (buf_idx < DATA_64_BITS)
        {
            raw_data = demod->data256.data0;
            bit_idx = buf_idx;
        }
        else if (buf_idx < DATA_128_BITS)
        {
            raw_data = demod->data256.data1;
            bit_idx = buf_idx - DATA_64_BITS;
        }
        else if (buf_idx < (DATA_128_BITS + HEADER_ORI_BITS))
        {
            raw_data = demod->data256.data2;
            bit_idx = buf_idx - DATA_128_BITS;
        }
        else
        {
            return DECODE_FAIL; /* Header beyond valid range */
        }

        /* Match header bit */
        uint8_t current_bit = (raw_data & (1ULL << bit_idx)) ? 1 : 0;
        uint8_t header_bit = (header_pattern & (1 << header_match_cnt)) ? 1 : 0;

        if (current_bit == header_bit)
        {
            header_match_cnt++;
            /* Header match complete (20 bits) */
            if (header_match_cnt == HEADER_EXTEN_BITS)
            {
                uint128_t decoded_128 = {.low = ORI_HEADER_MANCHER_BITS, .high = 0};
                buf_idx++;

                /* Extract 128-bit data */
                for (uint8_t i = 0; i < (DATA_128_BITS - HEADER_ORI_BITS); i++)
                {
                    uint32_t curr_idx = buf_idx + i;
                    if (curr_idx < DATA_64_BITS)
                    {
                        raw_data = demod->data256.data0;
                        bit_idx = curr_idx;
                    }
                    else if (curr_idx < DATA_128_BITS)
                    {
                        raw_data = demod->data256.data1;
                        bit_idx = curr_idx - DATA_64_BITS;
                    }
                    else if (curr_idx < (DATA_64_BITS * 3))
                    {
                        raw_data = demod->data256.data2;
                        bit_idx = curr_idx - DATA_128_BITS;
                    }
                    else if (curr_idx < DATA_256_BITS)
                    {
                        raw_data = demod->data256.data3;
                        bit_idx = curr_idx - (DATA_64_BITS * 3);
                    }

                    /* Set bit in 128-bit buffer */
                    uint8_t data_bit = (raw_data & (1ULL << bit_idx)) ? 1 : 0;
                    if ((i + HEADER_ORI_BITS) < DATA_64_BITS)
                    {
                        decoded_128.low |= (uint64_t)data_bit << (i + HEADER_ORI_BITS);
                    }
                    else
                    {
                        decoded_128.high |= (uint64_t)data_bit << (i + HEADER_ORI_BITS - DATA_64_BITS);
                    }
                }

                /* Convert 128-bit to 64-bit packet */
                for (uint8_t i = 0; i < 32; i++)
                {
                    if (decoded_128.low & (1ULL << (2 * i)))
                    {
                        data_packet.raw |= 1ULL << i;
                    }
                    if (decoded_128.high & (1ULL << (2 * i)))
                    {
                        data_packet.raw |= 1ULL << (i + 32);
                    }
                }

                /* Check parity */
                if (if_rfid_valid(data_packet))
                {
                    /* Assemble final 5-byte RFID data */
                    output[0] = (data_packet.field.d0 << 4) | data_packet.field.d1;
                    output[1] = (data_packet.field.d2 << 4) | data_packet.field.d3;
                    output[2] = (data_packet.field.d4 << 4) | data_packet.field.d5;
                    output[3] = (data_packet.field.d6 << 4) | data_packet.field.d7;
                    output[4] = (data_packet.field.d8 << 4) | data_packet.field.d9;

                    /* Swap bits to correct format */
                    for (uint8_t i = 0; i < RFID_DATA_LEN; i++)
                    {
                        output[i] = swap_data_bits(output[i]);
                    }

                    /* Reset demod buffer */
                    memset((void*)&demod->data256, 0, sizeof(uint256_t));
                    demod->decode_state = DECODE_IN_PROGRESS;
                    return DECODE_OK;
                }
                else
                {
                    return DECODE_FAIL;
                }
            }
        }
        else
        {
            /* Reset header match if bit mismatch */
            header_match_cnt = (current_bit == (header_pattern & 1)) ? 1 : 0;
        }
    }

    return DECODE_FAIL;
}

/**
 * @brief Input capture ISR callback
 * @param self: RFID handler
 */
void demo_algo_isr_cb(rfid_handler_t *const self)
{
    if (!self || !PRIV_DATA(self) || !PRIV_DATA(self)->is_initialized)
        return;

    rfid_demod_t *demod = &DEMOD_DATA(self);

    /* Check if 256-bit buffer is full */
    if (demod->data256_idx > (DATA_256_BITS - 1))
    {
        demod->data256_idx = 0;
        demod->decode_state = DECODE_OK;

        /* Safe semaphore release via injected interface */
        if (PRIV_DATA(self)->decode_complete_sem)
        {
            OS_IF(self)->pf_os_sema_give(PRIV_DATA(self)->decode_complete_sem);
        }
        return;
    }

    /* Get capture edge type */
    capture_edge_t edge = IO_CAPTURE(self)->pf_get_capture_edge();
    if (edge == EDGE_ERR)
    {
        return;
    }

    /* Read capture counter value */
    uint32_t capture_val = 0;
#if(!RFID_MORE_PRECISE)
    capture_val = IO_CAPTURE(self)->pf_get_counter() + 1;
    /* Clear counter for next capture */
    IO_CAPTURE(self)->pf_clear_counter();
#else
    uint32_t now_ic_value = IO_CAPTURE(self)->pf_get_ic_value(edge) + 1;
    if(now_ic_value > PRIV_DATA(self)->pre_ic_value)
        capture_val = now_ic_value - PRIV_DATA(self)->pre_ic_value;
    else/* overflow */
    {
        capture_val = now_ic_value + IO_CAPTURE(self)->pf_get_arr() + 1 - PRIV_DATA(self)->pre_ic_value;
    }
    PRIV_DATA(self)->pre_ic_value = now_ic_value;
    // RFID_DEBUG_OUT("ic_val=%d\r\n", capture_val);
#endif

    /* Process only when decoding is in progress */
    if (demod->decode_state != DECODE_IN_PROGRESS)
    {
        return;
    }

    rfid_wave_t wave_type = judge_wave_type(capture_val);
    if (wave_type == WAVE_INVALID)
    {
        /* Reset on invalid wave */
        demod->data256_idx = 0;
        memset((void*)&demod->data256, 0, sizeof(uint256_t));
        return;
    }

    /* Calculate bit count (0.5T=1bit, 1T=2bits) */
    uint32_t bit_count = (wave_type == WAVE_0P5T) ? 1 : 2;

    /* Process rising edge (Manchester '0') */
    if (edge == EDGE_RISING)
    {
        demod->data256_idx += bit_count;
    }
    /* Process falling edge (Manchester '1') */
    else if (edge == EDGE_FALLING)
    {
        /* Write '1' bits to 256-bit buffer */
        for (uint32_t i = 0; i < bit_count; i++)
        {
            uint32_t curr_idx = demod->data256_idx + i;
            if (curr_idx >= DATA_256_BITS)
                break;

            /* Write to corresponding 64-bit chunk */
            if (curr_idx < DATA_64_BITS)
            {
                demod->data256.data0 |= (1ULL << curr_idx);
            }
            else if (curr_idx < DATA_128_BITS)
            {
                demod->data256.data1 |= (1ULL << (curr_idx - DATA_64_BITS));
            }
            else if (curr_idx < (DATA_64_BITS * 3))
            {
                demod->data256.data2 |= (1ULL << (curr_idx - DATA_128_BITS));
            }
            else
            {
                demod->data256.data3 |= (1ULL << (curr_idx - (DATA_64_BITS * 3)));
            }
        }
        demod->data256_idx += bit_count;
    }
}

/**
 * @brief Read and decode RFID data
 * @param self: RFID handler
 * @param buf: Output buffer (5 bytes)
 * @return Decode status
 */
static rfid_decode_status_t read_rfid_data(rfid_handler_t *const self, uint8_t *const buf)
{
    uint8_t decoded_data[RFID_DATA_LEN] = {0};
    rfid_decode_status_t status = demodulate_rfid_data(self, decoded_data);

    if (status == DECODE_OK)
    {
        memcpy(buf, decoded_data, RFID_DATA_LEN);
        return DECODE_OK;
    }
    else if (status == DECODE_IN_PROGRESS)
    {
        return DECODE_IN_PROGRESS;
    }
    else if (status == DECODE_FAIL)
    {
        return DECODE_FAIL;
    }

    return DECODE_ERR_OTHER;
}

/**
 * @brief RFID handler RTOS thread
 * @param arg: RFID handler pointer
 */
/* true if the decoded tag equals the read-failed sentinel. */
static bool rfid_is_fail(const uint8_t *rfid)
{
    return (memcmp(rfid, (const uint8_t[])RFID_FAIL_CODE, RFID_DATA_LEN) == 0);
}

/* Reset the demod context so a retry starts from a clean capture buffer. */
static void rfid_reset_demod(rfid_handler_t *const self)
{
    rfid_demod_t *demod = &PRIV_DATA(self)->rfid_demod;
    memset((void*)&demod->data256, 0, sizeof(uint256_t));
    demod->decode_state = DECODE_IN_PROGRESS;
    demod->data256_idx = 0;
    PRIV_DATA(self)->pre_ic_value = 0;
}

static void rfid_handler_thread(void *arg)
{
    rfid_handler_t *const self = (rfid_handler_t *)arg;
    if (!self)
        return;

    rfid_os_interface_t *os_if = PRIV_DATA(self)->os_if;

    while (1)
    {
        /* Block until rfid_read_asy() sends a request. The bundled item carries
         * both the retry budget and the callback context, so concurrent callers
         * never clobber each other. */
        rfid_start_req_t req = {0};
        os_if->pf_os_queue_receive(PRIV_DATA(self)->start_queue, &req, RFID_OS_WAIT_FOREVER);

        uint8_t rfid_result[RFID_DATA_LEN] = RFID_FAIL_CODE;

        for (uint8_t attempt = 0; attempt < req.max_attempts; attempt++)
        {
            /* Reset demod context for a clean capture. */
            rfid_reset_demod(self);

            /* Enable RFID capture. */
            RFID_OPS(self)->pf_rfid_open();

            /* Wait for the capture buffer to fill (or DEMOD_TOTAL_MS to elapse). */
            int32_t sem_status = os_if->pf_os_sema_take(PRIV_DATA(self)->decode_complete_sem, DEMOD_TOTAL_MS);

            if (sem_status == 0)
            {
                read_rfid_data(self, rfid_result);
            }

            /* Disable RFID capture. */
            RFID_OPS(self)->pf_rfid_close();

            /* Stop early on the first successful decode. */
            if (!rfid_is_fail(rfid_result))
            {
                break;
            }
        }

        /* Invoke the completion callback exactly once with the final result
         * (decoded tag, or RFID_FAIL_CODE if every attempt failed). */
        if (req.cb_ctx && req.cb_ctx->pf_rfid_cpl_cb)
        {
            req.cb_ctx->pf_rfid_cpl_cb(rfid_result, req.cb_ctx->arg);
        }
    }
}

/**
 * @brief Initialize RFID handler
 * @param self: RFID handler
 * @param input_arg: Input arguments
 * @return Handler status
 */
rfid_handler_status_t rfid_handler_inst(rfid_handler_t *const self, rfid_handler_input_arg_t *const input_arg)
{
    /* Parameter validation */
    if (!self || !input_arg || !input_arg->io_capture_ops || !input_arg->rfid_ops || !input_arg->os_if)
    {
        return RFID_HANDLER_ERR_PARAM_INVALID;
    }

    /* Every injected OS function pointer must be present. */
    rfid_os_interface_t *os_if = input_arg->os_if;
    if (os_if->pf_os_thread_create == NULL || os_if->pf_os_sema_create == NULL ||
        os_if->pf_os_sema_delete == NULL || os_if->pf_os_sema_give == NULL ||
        os_if->pf_os_sema_take == NULL ||
        os_if->pf_os_queue_create == NULL || os_if->pf_os_queue_delete == NULL ||
        os_if->pf_os_queue_send == NULL || os_if->pf_os_queue_receive == NULL)
    {
        return RFID_HANDLER_ERR_PARAM_INVALID;
    }

    /*
     * Duplicate-initialization guard.
     *
     * Must run BEFORE the memset below. We only inspect self->magic here — a
     * single native-width read that never dereferences rfid_priv_data. This
     * makes the check safe even when self lives on the stack / in freshly
     * malloc'd memory whose rfid_priv_data still holds a stale garbage value.
     */
    if (self->magic == RFID_HANDLER_MAGIC)
    {
        /* Magic is present; private data must be valid and marked initialized. */
        if (PRIV_DATA(self) && PRIV_DATA(self)->is_initialized)
        {
            return RFID_HANDLER_ERR_ALREADY_INITED;
        }
    }

    /*
     * Zero the instance. This neutralises any wild pointer left in
     * rfid_priv_data (or any other field) by the caller's storage class before
     * we start dereferencing anything. Safe now because the duplicate-init guard
     * above has already captured the "already initialised" state.
     */
    memset(self, 0, sizeof(*self));

    /* Check required function pointers */
    if (!input_arg->io_capture_ops->pf_get_capture_edge ||
        !input_arg->rfid_ops->pf_rfid_init ||
        !input_arg->rfid_ops->pf_rfid_open ||
#if(!RFID_MORE_PRECISE)
        !input_arg->io_capture_ops->pf_get_counter||
        !input_arg->io_capture_ops->pf_clear_counter ||
#else
        !input_arg->io_capture_ops->pf_get_ic_value||
        !input_arg->io_capture_ops->pf_get_arr||
#endif
        !input_arg->rfid_ops->pf_rfid_close)
    {
        return RFID_HANDLER_ERR_PARAM_INVALID;
    }

    PRIV_DATA(self) = MALLOC(sizeof(rfid_priv_data_t));
    if (!PRIV_DATA(self))
        return RFID_HANDLER_ERR_OTHERS;
    memset(PRIV_DATA(self), 0, sizeof(*PRIV_DATA(self)));

    /* Store injected OS interface. */
    PRIV_DATA(self)->os_if = os_if;
    self->input_arg = input_arg;

    /* Init RFID hardware */
    RFID_OPS(self)->pf_rfid_init();

    /* Create the completion semaphore and start queue BEFORE the thread so the
     * thread never blocks on a not-yet-created object (the original code created
     * them in the opposite order — a latent race once the handler thread preempts). */
    int32_t ret;
    ret = os_if->pf_os_sema_create(&PRIV_DATA(self)->decode_complete_sem, 1, 0);
    if (ret != 0)
    {
        FREE(PRIV_DATA(self));
        PRIV_DATA(self) = NULL;
        return RFID_HANDLER_ERR_OTHERS;
    }
    /* Queue depth 1 is enough: each request is consumed immediately by the thread. */
    ret = os_if->pf_os_queue_create(1, sizeof(rfid_start_req_t), &PRIV_DATA(self)->start_queue);
    if (ret != 0)
    {
        os_if->pf_os_sema_delete(PRIV_DATA(self)->decode_complete_sem);
        FREE(PRIV_DATA(self));
        PRIV_DATA(self) = NULL;
        return RFID_HANDLER_ERR_OTHERS;
    }

    /* Create RTOS thread */
    void *thread_handle = NULL;
    ret = os_if->pf_os_thread_create(
        "RFID_HANDLER",
        rfid_handler_thread,
        RFID_HANDLER_THREAD_STACK_DEPTH,
        RFID_HANDLER_THREAD_PRIORITY,
        &thread_handle,
        (void *)self);

    if (ret != 0 || thread_handle == NULL)
    {
        os_if->pf_os_sema_delete(PRIV_DATA(self)->decode_complete_sem);
        os_if->pf_os_queue_delete(PRIV_DATA(self)->start_queue);
        FREE(PRIV_DATA(self));
        PRIV_DATA(self) = NULL;
        return RFID_HANDLER_ERR_OTHERS;
    }

    PRIV_DATA(self)->thread_handle = thread_handle;

    /* Mark instance as initialized.
     * magic is stamped last so a partially-initialised instance (e.g. one that
     * bails out due to an allocation/os-call failure mid-way) is never mistaken
     * for a live one by the duplicate-init guard.
     */
    PRIV_DATA(self)->is_initialized = true;
    self->magic = RFID_HANDLER_MAGIC;
    return RFID_HANDLER_OK;
}

/**
 * @brief Start an asynchronous RFID read with retry.
 *
 * The handler thread performs up to max_attempts capture/decode cycles, retrying
 * whenever a cycle yields RFID_FAIL_CODE (all 0xFF). The completion callback is
 * invoked exactly once — with the decoded tag, or RFID_FAIL_CODE if every
 * attempt failed.
 *
 * @param self         Initialised handler instance.
 * @param cb_ctx       Completion callback + user arg, invoked once when the
 *                     whole read (including retries) finishes.
 * @param max_attempts Maximum number of capture attempts (>= 1).
 * @return Handler status
 */
rfid_handler_status_t rfid_read_asy(rfid_handler_t *const self,
                                    rfid_cpl_cb_ctx_t *cb_ctx,
                                    uint8_t max_attempts)
{
    if (!self || !PRIV_DATA(self) || !PRIV_DATA(self)->is_initialized || !cb_ctx)
    {
        return RFID_HANDLER_ERR_HANDLER_NOT_READY;
    }

    rfid_start_req_t req =
    {
        .max_attempts = (max_attempts > 0U) ? max_attempts : 1U,
        .cb_ctx       = cb_ctx,
    };
    OS_IF(self)->pf_os_queue_send(PRIV_DATA(self)->start_queue, &req, RFID_OS_WAIT_FOREVER);

    return RFID_HANDLER_OK;
}