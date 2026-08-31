#include "protocol_high_relia.h"
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct
{
    uint8_t fun_coude;
    uint8_t frame_ind;
    bool if_uplink;
    uint8_t sensor_id[6];
    uint16_t len;
    uint8_t *payload;
} high_relia_para_t;

/* Private instance state. msg_no is the frame counter: starts at 0, ++ per uplink. */
struct high_relia_priv_data
{
    uint8_t msg_no;
};

static uint16_t high_reliable_crc16(uint8_t *p, uint16_t len, uint16_t init_value)
{
    uint16_t crc = init_value, i = 0;
    uint8_t  b;

    for (i = 0; i < len; i++)
    {
        for (b = 0x80; b != 0; b >>= 1)
        {
            if ((crc & 0x8000) != 0)
            {
                crc <<= 1;
                crc ^= 0x1021;
            }
            else crc <<= 1;
            if ((*p & b) != 0) crc ^= 0x1021;
        }
        p++;
    }
    return crc;
}

/* Protocol frame constants ----------------------------------------------- */
#define PACK_HEAD               0x14
#define CTRL_TYPE_BASE          0x10
#define ANTI_ERR_TYPE_BYTE0     0x81
#define ANTI_ERR_TYPE_BYTE1     0x96
#define LEN_FIELD_CRC_SIZE      2   /* the length field covers itself + the trailing CRC */
#define CRC16_INIT_VALUE        0xFFFF

#define FRAME_HEAD_SIZE         (6 + 1 + 1 + 2 + 2 + 1 + 1)  /* sensor_id + pack_head + ctrl + anti_err + len + fun_coude + frame_ind */
#define FRAME_TAIL_SIZE         sizeof(uint16_t)             /* crc16 */

/* Build the high-reliable frame directly into the caller's buffer.
 * Returns 0 on success, -1 on bad args or insufficient buffer space. */
static int8_t high_reliable_wrapper(high_relia_para_t *const high_relia_para,
                                     uint8_t *out_buf, uint16_t out_buf_size,
                                     uint16_t *out_len)
{
    if (!high_relia_para || !out_buf || !out_len || !high_relia_para->payload)
    {
        return -1;
    }

    uint16_t data_len = FRAME_HEAD_SIZE + high_relia_para->len + FRAME_TAIL_SIZE;
    if (data_len > out_buf_size)
    {
        return -1;
    }

    uint8_t index = 0;

    {
        memcpy(out_buf + index, high_relia_para->sensor_id, sizeof(high_relia_para->sensor_id));
        index += sizeof(high_relia_para->sensor_id);
    }
    {
        out_buf[index++] = PACK_HEAD;
    }
    {
        out_buf[index++] = CTRL_TYPE_BASE | high_relia_para->if_uplink;
    }
    {
        out_buf[index++] = ANTI_ERR_TYPE_BYTE0;
        out_buf[index++] = ANTI_ERR_TYPE_BYTE1;
    }
    {
        uint16_t len = high_relia_para->len + LEN_FIELD_CRC_SIZE;
        memcpy(out_buf + index, (uint8_t *)&len, sizeof(len));
        index += sizeof(len);
    }
    {
        out_buf[index++] = high_relia_para->fun_coude;
    }
    {
        out_buf[index++] = high_relia_para->frame_ind;
    }
    {
        memcpy(out_buf + index, high_relia_para->payload, high_relia_para->len);
        index += high_relia_para->len;
    }
    {
        uint16_t crc16 = high_reliable_crc16(out_buf, index, CRC16_INIT_VALUE);
        memcpy(out_buf + index, (uint8_t *)&crc16, sizeof(crc16));
        index += sizeof(crc16);
    }

    *out_len = index;
    return 0;
}

int8_t high_relia_inst(high_relia_handler_t *const self, const high_relia_ops_t *ops)
{
    if (self == NULL || ops == NULL)
    {
        return -1;
    }

    /* Duplicate-initialization guard.
     *
     * Must run BEFORE the memset below. We only inspect self->magic here -- a
     * single native-width read that never dereferences priv_data. This makes
     * the check safe even when self lives on the stack / in freshly malloc'd
     * memory whose priv_data still holds a stale garbage value.
     */
    if (self->magic == HIGH_RELIABLE_MAGIC)
    {
        return -1;
    }

    /*
     * Zero the instance. This neutralises any wild pointer left in priv_data
     * (or any other field) by the caller's storage class before we start
     * dereferencing anything. Safe now because the duplicate-init guard above
     * has already captured the "already initialised" state.
     */
    memset(self, 0, sizeof(*self));

    /* Allocate private state */
    high_relia_priv_data_t *priv = malloc(sizeof(high_relia_priv_data_t));
    if (priv == NULL)
    {
        return -1;
    }
    priv->msg_no = 0;

    self->ops       = *ops;
    self->priv_data = priv;

    /* Finalize initialization.
     * magic is stamped last so a partially-initialised instance (e.g. one that
     * bails out due to an allocation failure mid-way) is never mistaken for a
     * live one by the duplicate-init guard.
     */
    self->magic = HIGH_RELIABLE_MAGIC;
    return 0;
}

int8_t send_info_high_reliable_uplink(high_relia_handler_t *const self, MOUNT_STATE state)
{
    uint8_t send_buf[64] = {0};
    uint16_t frame_len = 0;

    /* Guard: instance initialised and all three operations injected. */
    if (self == NULL || self->magic != HIGH_RELIABLE_MAGIC ||
        self->ops.pf_get_rfid == NULL ||
        self->ops.pf_get_uid  == NULL ||
        self->ops.pf_send     == NULL)
    {
        return -1;
    }

    send_buf[0] = 0x13;
    send_buf[1] = self->priv_data->msg_no++;
    send_buf[2] = 0x20;

    /* mounting state */
    uint8_t mounting_state = (state == H_MOUNT) ? 1 : 0;

    uint8_t payload[7] = {0};
    if (self->ops.pf_get_rfid(payload) != 0)
    {
        return -1;
    }
    payload[6] = mounting_state;

    uint8_t sensor_id[6] = {0xDE, 0xAD, 0xBE, 0xEF};
    if (self->ops.pf_get_uid(sensor_id) != 0)
    {
        return -1;
    }

    high_relia_para_t high_relia_para =
    {
        .if_uplink = 1,
        .fun_coude = 0x45,
        .frame_ind = send_buf[1],
        .len = sizeof(payload),
        .payload = payload,
    };
    memcpy(high_relia_para.sensor_id, sensor_id, 6);

    if (high_reliable_wrapper(&high_relia_para, &send_buf[3], sizeof(send_buf) - 3, &frame_len) != 0)
    {
        return -1;
    }

    return self->ops.pf_send(send_buf, 3 + frame_len);
}
