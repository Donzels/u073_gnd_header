#include "stdint.h"
#include "stdbool.h"

/* -------------------------------------------------------------------------- */
/*                    Injectible operations (dependency injection)             */
/* -------------------------------------------------------------------------- */

typedef enum
{
    H_UNMOUNT = 0,
    H_MOUNT
} MOUNT_STATE;

/* Read the 6-byte RFID tag. Returns 0 on success. */
typedef int8_t (*pf_get_rfid_6bytes)(uint8_t *rfid);

/* Read the 6-byte unique device ID. Returns 0 on success. */
typedef int8_t (*pf_get_unique_id_6bytes)(uint8_t *uid);

/* Transmit a frame over Zigbee. Returns 0 on success. */
typedef int8_t (*pf_send_data)(const uint8_t *data, uint16_t len);

/* Table of operations injected into the protocol at init time. */
typedef struct
{
    pf_get_rfid_6bytes      pf_get_rfid;
    pf_get_unique_id_6bytes pf_get_uid;
    pf_send_data            pf_send;
} high_relia_ops_t;

#define HIGH_RELIABLE_MAGIC   0x52454C49U  /* "RELI" */

typedef struct high_relia_priv_data high_relia_priv_data_t;

typedef struct
{
    uint32_t                magic;      /* HIGH_RELIABLE_MAGIC only after full init succeeds */
    high_relia_ops_t        ops;        /* injected operations */
    high_relia_priv_data_t *priv_data;  /* private state (msg_no, etc.) */
} high_relia_handler_t;

/* Initialise an instance. Returns 0 on success, -1 on bad args or already init'd. */
int8_t high_relia_inst(high_relia_handler_t *const self, const high_relia_ops_t *ops);

/* Send an uplink. Returns 0 on success, -1 on error. */
int8_t send_info_high_reliable_uplink(high_relia_handler_t *const self, MOUNT_STATE state);
