#include "upper_commu.h"
#include "wapi_commu.h"   /* wapi_send, wapi_config_init, SLEEPTIME_* */
#include "low_power.h"



#define IS_MB_MSB           0/* modbus rule is MSB, but some use LSB */

#define MB_TX_BUF_SIZE      128

/* holding register mapping */
/* part 0 */
#define REG_ADDR_TARGET_IP_0        7
#define REG_ADDR_TARGET_IP_1        8
#define REG_ADDR_TARGET_IP_2        9
#define REG_ADDR_TARGET_IP_3        10
#define REG_ADDR_TARGET_NET_PORT    11
/* part 1 */
#define REG_ADDR_PROGRAM_TYPE       128
#define REG_ADDR_TOTAL_FRAME        129
#define REG_ADDR_CUR_FRAME          130
#define REG_ADDR_FILE_SIZE          131 /* occupy 2 reg */
#define REG_ADDR_DATA_CRC           133
#define REG_ADDR_DATA_START         134
#define REG_ADDR_DATA_END           197
/* part 2 */
#define REG_ADDR_LOCAL_IP_0         400
#define REG_ADDR_LOCAL_IP_1         401
#define REG_ADDR_LOCAL_IP_2         402
#define REG_ADDR_LOCAL_IP_3         403
#define REG_ADDR_LOCAL_MAC          404 /* occupy 3 reg */
#define REG_ADDR_GATEWAY_0          407
#define REG_ADDR_GATEWAY_1          408
#define REG_ADDR_GATEWAY_2          409
#define REG_ADDR_GATEWAY_3          410
#define REG_ADDR_SSID               411 /* occupy 16 reg */
#define REG_ADDR_PWD                427 /* occupy 8 reg */
#define REG_ADDR_CNN_MODE           435 /* 0:WAPI, 1:WIFI */
#define REG_ADDR_PHY_CNN_STATE      436 /* 0:disconnect, 1:connected */
#define REG_ADDR_TCP_CNN_STATE      437 /* 0:disconnect, 1:connected */
#define REG_ADDR_CERT_STATE         438 /* 0:certificate non-exist, 1:certificate exist */
/* part 3 (read-only) */
#define REG_ADDR_SW_VERSION         102
#define REG_ADDR_HW_VERSION         103
#define REG_ADDR_BUILD_YEAR         104
#define REG_ADDR_BUILD_MONTH        105
#define REG_ADDR_BUILD_DAY          106
#define REG_ADDR_BUILD_HOUR         107
#define REG_ADDR_BUILD_MIN          108
#define REG_ADDR_BUILD_SEC          109

#define SW_VERSION          100
#define HW_VERSION          100

#include "algo_data_integrity.h"
#define UPP_COMMU_DATA_INTEGRITY_ALGO(buf, len)     checksum_16bit(buf, len)

static void mb_slave_parse(uint8_t *const p_data, uint16_t data_len, void *arg);
static void mb_slave_parse_filter(uint8_t *const p_data, uint16_t data_len, void *arg);
static void mb_reg_part0_cb(uint16_t reg_addr, uint16_t reg_num);
static void mb_reg_part1_cb(uint16_t reg_addr, uint16_t reg_num);
static void mb_reg_part2_cb(uint16_t reg_addr, uint16_t reg_num);
static void synchro_read_only_info_2_holding_reg(void);

typedef enum
{
    FILE_PROGRAM = 1,
    FILE_PEM,
    FILE_CA,
    FILE_KEY,
    FILE_AS,
    FILE_ASUE
}file_type_t;

typedef enum
{
    SERVER_PART = 0,
    PROGRAM_PART,
    LOCAL_CONFIG_PART,
    READ_ONLY_PART
}mb_part_type_t;

typedef struct
{
    uint16_t reg_start_index;
    uint16_t reg_end_index;
    uint16_t *reg_addr;
    void (*pf_mb_reg_cb)(uint16_t reg_addr, uint16_t reg_num);  
}mb_holding_reg_part_t;

typedef struct
{
    bool if_init;
#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
    uart_proto_t *uart_handle;
#endif
    agile_modbus_rtu_t  *mb_handle;          
}upper_com_pri_data_t;

typedef struct 
{
    storage_ops_t *storage_ops;
    wapi_info_t wapi_info;    
}user_save_info_t;

typedef struct
{
    storage_ops_t *storage_ops;
    cert_file_t cert_file;
}cert_file_info_t;

#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
extern uart_ops_t g_upp_uart_ops;
extern recv_buf_att_t g_upp_uart_rx_buf;
extern uart_rx_os_interface_t g_uart_os_interface;
#endif

static wapi_info_t g_config_wapi_info = 
{
    .server_ip = {10,0,0,66},
    .server_port = 666,
    .local_port = 777,
    .local_ip = {10,0,0,77},
    .local_ip_mask = {255,0,0,0},
    .local_gateway = {10,0,0,1},
    .ssid = "wapi_cfg",
    .pwd = "12345678",
    .is_exist_certicate = false
};


static agile_modbus_rtu_t g_mb_slave_rtu = {0};

static uint8_t mb_tx_buf[MB_TX_BUF_SIZE];

static  __attribute__((aligned(4))) uint16_t holding_part_0[REG_ADDR_TARGET_NET_PORT-REG_ADDR_TARGET_IP_0+1] = {0};
static  __attribute__((aligned(4))) uint16_t holding_part_1[REG_ADDR_DATA_END-REG_ADDR_PROGRAM_TYPE+1] = {0};
static  __attribute__((aligned(4))) uint16_t holding_part_2[REG_ADDR_CERT_STATE-REG_ADDR_LOCAL_IP_0+1] = {0};
static  __attribute__((aligned(4))) uint16_t holding_part_3[REG_ADDR_BUILD_SEC-REG_ADDR_SW_VERSION+1] = {0};

static mb_holding_reg_part_t g_mb_holding_reg_part[] =
{
    {REG_ADDR_TARGET_IP_0, REG_ADDR_TARGET_NET_PORT, holding_part_0, mb_reg_part0_cb},
    {REG_ADDR_PROGRAM_TYPE, REG_ADDR_DATA_END, holding_part_1, mb_reg_part1_cb},
    {REG_ADDR_LOCAL_IP_0, REG_ADDR_CERT_STATE, holding_part_2, mb_reg_part2_cb},
    {REG_ADDR_SW_VERSION, REG_ADDR_BUILD_SEC, holding_part_3, NULL},
};

#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
static parse_algo_t upp_parse_algo =
{
    .algo_type = ALGO_TRANSPARENT,
    .u ={.transparent_algo = {.arg = NULL, .pf_transparent_parse = mb_slave_parse_filter}}
};

static frame_parse_att_t upp_frame_parse_att = 
{
    .parse_algo = &upp_parse_algo,
    .recv_buf_att = &g_upp_uart_rx_buf
};

static rx_thread_att_t upp_rx_thread_att = 
{
    .parse_thread_att = 
    {
        .stack_depth = UPP_COMMU_PARSE_THREAD_STACK_DEPTH,
        .thread_priority = UPP_COMMU_PARSE_THREAD_PRIORITY - 1
    }
};

static uart_proto_input_arg_t upp_uart_proto_input_arg = 
{
    .frame_parse_att = &upp_frame_parse_att,
    .os_interface = &g_uart_os_interface,
    .uart_ops = &g_upp_uart_ops,
    .thread_att = &upp_rx_thread_att
};

static uart_proto_t upp_uart_proto_instance = {0};
#endif

static upper_com_pri_data_t upper_com_pri_data = 
{
    .if_init = false,
    .mb_handle = NULL,
#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
    .uart_handle = NULL
#endif
};

static user_save_info_t g_user_save_info = 
{
    .storage_ops = NULL
};

static cert_file_info_t g_cert_file_info = 
{
    .storage_ops = NULL
};

/**
 * @brief  Slave callback function: handle holding register read/write requests
 * @param  ctx: modbus handle
 * @param  slave_info: slave request information
 * @param  data: private data (holding register storage area passed here)
 * @return 0=normal; <0=exception (e.g., return corresponding Modbus exception code for register out of bounds)
 */
static int modbus_slave_cb(agile_modbus_t *ctx, \
                            struct agile_modbus_slave_info *slave_info, const void *data)
{
    /* uint16_t *hold_regs = (uint16_t *)data; */
    int reg_addr = slave_info->address;    /* register start address */
    int reg_num = slave_info->nb;          /* register count */
    int func_code = slave_info->sft->function; /* function code */
    int ret = 0;

    int8_t part_index = -1;
    /* 1. register address validity check */
    for(uint8_t i=0; i<sizeof(g_mb_holding_reg_part)/sizeof(g_mb_holding_reg_part[0]); i++)
    {
        if(reg_addr>=g_mb_holding_reg_part[i].reg_start_index && \
                            (reg_addr+reg_num-1)<=g_mb_holding_reg_part[i].reg_end_index)
        {
            part_index = i;
            break;
        }
    }
    if(-1 == part_index) /* register address not match */
        return -AGILE_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;

    uint16_t *hold_regs = g_mb_holding_reg_part[part_index].reg_addr; 
    reg_addr -= g_mb_holding_reg_part[part_index].reg_start_index;

    /* 2. handle read/write logic by function code */
    switch (func_code) 
    {
        /* --------------- write multiple holding registers (0x10) --------------- */
        case 0x10: 
        {
            /* multiple register values: parse from request buffer (2 bytes per register) */
            for (int i = 0; i < reg_num; i++) 
            {
                uint16_t reg_val = agile_modbus_slave_register_get(slave_info->buf, i);
#if(!IS_MB_MSB)
                uint16_t swap_val = ((reg_val & 0x00FF) << 8) | ((reg_val & 0xFF00) >> 8);  
                reg_val = swap_val;
#endif                
                hold_regs[reg_addr + i] = reg_val;
            }
            if(g_mb_holding_reg_part[part_index].pf_mb_reg_cb)
               g_mb_holding_reg_part[part_index].pf_mb_reg_cb(reg_addr + \
                                g_mb_holding_reg_part[part_index].reg_start_index, reg_num); 
            break;
        }
        /* --------------- read holding registers (0x03) --------------- */
        case 0x03: 
        {
            /* copy register data to response buffer (slave_info->buf) */
            for (int i = 0; i < reg_num; i++) 
            {
                uint16_t reg_val = hold_regs[reg_addr + i];  
#if(!IS_MB_MSB)                
                uint16_t swap_val = ((reg_val & 0x00FF) << 8) | ((reg_val & 0xFF00) >> 8);  
                reg_val = swap_val;    
#endif 
                agile_modbus_slave_register_set(ctx->send_buf + slave_info->send_index, i, reg_val);
            }
            break;
        }
        /* --------------- write single holding register (0x06) --------------- */
        case 0x06:  
        {
            uint16_t reg_val = *((uint16_t *)slave_info->buf);
#if(!IS_MB_MSB)
            uint16_t swap_val = ((reg_val & 0x00FF) << 8) | ((reg_val & 0xFF00) >> 8);  
            reg_val = swap_val; 
#endif                
            hold_regs[reg_addr]= reg_val;
            if(g_mb_holding_reg_part[part_index].pf_mb_reg_cb)
               g_mb_holding_reg_part[part_index].pf_mb_reg_cb(reg_addr + \
                                        g_mb_holding_reg_part[part_index].reg_start_index, 1); 
            break;
        }        
        

        /* --------------- unsupported function code --------------- */
        default:
            ret = -AGILE_MODBUS_EXCEPTION_ILLEGAL_FUNCTION; /* illegal function code */
            break;
    }

    return ret;
}

static void mb_slave_parse(uint8_t *const p_data, uint16_t data_len, void *arg) 
{
    if(!upper_com_pri_data.if_init)
        return;
    agile_modbus_t *ctx = &upper_com_pri_data.mb_handle->_ctx;
    ctx->read_buf = p_data;
    int rsp_len = 0;          /* response data length */ 
    int frame_len = 0;        /* Modbus frame total length */
    rsp_len = agile_modbus_slave_handle(
            ctx,                    /* Modbus handle */
            data_len,               /* received data length */
            1,           /* strict slave address check */
            modbus_slave_cb,        /* callback function (handle read/write logic) */
            NULL,              /* private data (holding register storage) */
            &frame_len              /* output: Modbus frame length */
        );
    /* respond to master (if rsp_len >= 0, send response data) */
    if (rsp_len > 0)
    {
#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
        upper_com_pri_data.uart_handle->uart_proto_input_arg->uart_ops->pf_uart_write(ctx->send_buf, rsp_len);
#else
        wapi_send(ctx->send_buf, rsp_len);
#endif
        low_power_schedule_sleep_ms(SLEEPTIME_CONFIG_MS, true);
    }
}

/**
 * @brief  Filter wrapper layered on top of mb_slave_parse
 *
 *          Strips the AT "+RECV[<id>][<len>]:" header, hex-decodes the
 *          payload that follows the ':' into binary, then forwards it to
 *          mb_slave_parse. Input sample (text, 21 bytes):
 *
 *              +RECV[1][8]:010300000001840A
 *
 *          Only the hex part after ':' (010300000001840A -> 8 bytes) is
 *          handed to mb_slave_parse. If the text does not match the format,
 *          the data is silently dropped.
 */
static void mb_slave_parse_filter(uint8_t *const p_data, uint16_t data_len, void *arg)
{
    /* --- 1. locate the field separator ':' --- */
    uint8_t *colon = (uint8_t *)memchr(p_data, ':', data_len);
    if (colon == NULL)
        return;                              /* not a +RECV frame: drop */

    uint8_t *hex = colon + 1;
    uint16_t hex_len = (uint16_t)(data_len - (hex - p_data));
    if (hex_len == 0 || (hex_len & 1))       /* empty or odd nibble count */
        return;

    /* --- 2. hex -> binary decode in place (halve the buffer) --- */
    uint16_t bin_len = hex_len >> 1;
    for (uint16_t i = 0; i < bin_len; i++)
    {
        uint8_t hi = hex[i << 1];
        uint8_t lo = hex[(i << 1) + 1];

        uint8_t nib_hi = (uint8_t)((hi <= '9') ? (hi - '0') : \
                         (hi <= 'F') ? (hi - 'A' + 10) : (hi - 'a' + 10));
        uint8_t nib_lo = (uint8_t)((lo <= '9') ? (lo - '0') : \
                         (lo <= 'F') ? (lo - 'A' + 10) : (lo - 'a' + 10));

        hex[i] = (uint8_t)((nib_hi << 4) | nib_lo);
    }

    /* --- 3. forward the decoded Modbus frame to mb_slave_parse --- */
    mb_slave_parse(hex, bin_len, arg);
}

static void save_user_data(void)
{
    storage_ops_t *storage_ops = g_user_save_info.storage_ops;
    if(!storage_ops->pf_if_addr_valid(storage_ops->save_addr + sizeof(wapi_info_t)))
    {
        UP_COMMU_DEBUG_OUT("program addr invalid!\r\n"); 
        return;
    }    
    storage_ops->pf_stroage_erase(storage_ops->save_addr, sizeof(wapi_info_t));
    storage_ops->pf_stroage_write(storage_ops->save_addr,\
                            (uint8_t *)&g_user_save_info.wapi_info ,sizeof(wapi_info_t));        
}

static void recover_user_data(void)
{
    g_user_save_info.storage_ops->pf_stroage_read(g_user_save_info.storage_ops->save_addr,\
                            (uint8_t *)&g_user_save_info.wapi_info ,sizeof(wapi_info_t));
    if(!is_wapi_info_valid(&g_user_save_info.wapi_info)) 
    {
        reset_wapi_info(&g_user_save_info.wapi_info);
        save_user_data();
    }                   
}

static void save_file(uint8_t file_type, uint32_t offset, uint8_t *data, uint16_t len)
{
    if(FILE_AS != file_type && FILE_ASUE != file_type)
        return;
    const uint32_t each_file_max_size = 2048; 
    storage_ops_t *storage_ops = g_cert_file_info.storage_ops;
    uint32_t file_offset = offset;    
    if(FILE_ASUE == file_type)
        file_offset += each_file_max_size;    
    if(!storage_ops->pf_if_addr_valid(storage_ops->save_addr + file_offset + len))
    {
        UP_COMMU_DEBUG_OUT("program addr invalid!\r\n"); 
        return;
    }
    if(0 == (storage_ops->save_addr + file_offset) % storage_ops->min_erase_size) 
    {
        UP_COMMU_DEBUG_OUT("erase addr=0x%x\r\n", storage_ops->save_addr + file_offset); 
        storage_ops->pf_stroage_erase(storage_ops->save_addr + file_offset,\
                                         storage_ops->min_erase_size);
    }        
    storage_ops->pf_stroage_write(storage_ops->save_addr + file_offset, data, len);        
}

static void save_file_append(uint8_t file_type, uint32_t total_len)
{
    if(FILE_AS != file_type && FILE_ASUE != file_type)
        return;
    const uint32_t each_file_max_size = 2048; 
    storage_ops_t *storage_ops = g_cert_file_info.storage_ops;
    uint16_t digest = 0;
    /* save file append information in the final 128 bytes */
    uint32_t write_offset = each_file_max_size - 128; 
    uint32_t payload_offset = storage_ops->save_addr;  
    if(FILE_ASUE == file_type)
    {
        write_offset += each_file_max_size;
        payload_offset += each_file_max_size;    
    }           
    if(!storage_ops->pf_if_addr_valid(storage_ops->save_addr + write_offset))
    {
        UP_COMMU_DEBUG_OUT("program addr invalid!\r\n"); 
        return;
    }
    digest = UPP_COMMU_DATA_INTEGRITY_ALGO((uint8_t*)payload_offset, total_len);
    if(0 == (storage_ops->save_addr + write_offset) % storage_ops->min_erase_size) 
    {
        UP_COMMU_DEBUG_OUT("erase addr=0x%x\r\n", storage_ops->save_addr + write_offset); 
        storage_ops->pf_stroage_erase(storage_ops->save_addr + write_offset,\
                                         storage_ops->min_erase_size);
    }  
    file_append_t file_append = 
    {
        .file_len = total_len,
        .digest = digest
    };
    
    storage_ops->pf_stroage_write(storage_ops->save_addr + write_offset,\
                                 (uint8_t*)&file_append, sizeof(file_append_t));       
}

static bool if_cert_file_valid(void)
{
    const uint32_t each_file_max_size = 2048; 
    storage_ops_t *storage_ops = g_cert_file_info.storage_ops;
    /* validate AS file */
    uint32_t payload_offset = storage_ops->save_addr;
    uint32_t append_offset = storage_ops->save_addr + each_file_max_size - 128;

    file_att_t *file_att = &g_cert_file_info.cert_file.as_file;
    for(uint8_t i=0; i<2; i++)
    {
        /* validate ASUE file */
        if(1 == i)
        {
            payload_offset += each_file_max_size;
            append_offset += each_file_max_size;
            file_att = &g_cert_file_info.cert_file.asue_file;            
        }
        file_att->file_payload = (uint8_t *)payload_offset;
        file_append_t *file_append_candidate = &file_att->file_append;
        storage_ops->pf_stroage_read(append_offset, (uint8_t*)file_append_candidate,\
                                     sizeof(file_append_t)); 
        if (file_append_candidate->file_len > each_file_max_size -128)
        {
            return false;
        }
        if(file_append_candidate->digest != UPP_COMMU_DATA_INTEGRITY_ALGO((uint8_t*)payload_offset,\
                        file_append_candidate->file_len))
            return false;
            
    }  
    return true;           
}

static void update_cert_state(void)
{
    mb_holding_reg_part_t *mb_holding_reg_part = &g_mb_holding_reg_part[LOCAL_CONFIG_PART];
    uint16_t *reg = mb_holding_reg_part->reg_addr - mb_holding_reg_part->reg_start_index;
    /* update certificate state */
    reg[REG_ADDR_CERT_STATE] = g_user_save_info.wapi_info.is_exist_certicate =  if_cert_file_valid();   
}

static void mb_reg_part0_cb(uint16_t reg_addr, uint16_t reg_num)
{
    wapi_info_t *const wapi_info = &g_user_save_info.wapi_info;  
    mb_holding_reg_part_t *mb_holding_reg_part = &g_mb_holding_reg_part[SERVER_PART];
    uint16_t *reg = mb_holding_reg_part->reg_addr - mb_holding_reg_part->reg_start_index;
    for(uint8_t i=0; i<4; i++)
    {
        memcpy(&wapi_info->server_ip[i], &reg[REG_ADDR_TARGET_IP_0 + i], sizeof(uint8_t));
    }
    memcpy(&wapi_info->server_port, &reg[REG_ADDR_TARGET_NET_PORT], sizeof(uint16_t));   
    validate_wapi_info(wapi_info);
    save_user_data();

} 

static void mb_reg_part1_cb(uint16_t reg_addr, uint16_t reg_num)
{
    const uint8_t once_data_len = 64;
    mb_holding_reg_part_t *mb_holding_reg_part = &g_mb_holding_reg_part[PROGRAM_PART];
    uint16_t *reg = mb_holding_reg_part->reg_addr - mb_holding_reg_part->reg_start_index; 
    uint8_t file_type = reg[REG_ADDR_PROGRAM_TYPE] & 0x00FF;
    uint16_t cur_frame = ((reg[REG_ADDR_CUR_FRAME] & 0x00FF)<<8) | \
                            ((reg[REG_ADDR_CUR_FRAME] & 0xFF00)>>8); /* start from 1 */
    uint16_t total_frame = ((reg[REG_ADDR_TOTAL_FRAME] & 0x00FF)<<8) | \
                            ((reg[REG_ADDR_TOTAL_FRAME] & 0xFF00)>>8);
    
    uint32_t offset = cur_frame * once_data_len;
    save_file(file_type, offset, (uint8_t *)&reg[REG_ADDR_DATA_START], once_data_len); 
    /* last frame */
    if(total_frame - 1 == cur_frame)
    {
        uint16_t file_len = ((reg[REG_ADDR_FILE_SIZE+1] & 0x00FF)<<8) |\
                             ((reg[REG_ADDR_FILE_SIZE+1] & 0xFF00)>>8);
        save_file_append(file_type, file_len);        
        update_cert_state();
        // m0804c_cert_upload(&g_wapi_handler_inst);
    }                   
} 

static void mb_reg_part2_cb(uint16_t reg_addr, uint16_t reg_num)
{
    wapi_info_t *const wapi_info = &g_user_save_info.wapi_info;  
    mb_holding_reg_part_t *mb_holding_reg_part = &g_mb_holding_reg_part[LOCAL_CONFIG_PART];
    uint16_t *reg = mb_holding_reg_part->reg_addr - mb_holding_reg_part->reg_start_index;
    for(uint8_t i=0; i<4; i++)
    {
        memcpy(&wapi_info->local_ip[i], &reg[REG_ADDR_LOCAL_IP_0 + i], sizeof(uint8_t));
        memcpy(&wapi_info->local_gateway[i], &reg[REG_ADDR_GATEWAY_0 + i], sizeof(uint16_t)); 
    }
    memcpy(&wapi_info->ssid, &reg[REG_ADDR_SSID], sizeof(uint16_t)*(REG_ADDR_PWD - REG_ADDR_SSID));  
    memcpy(&wapi_info->pwd, &reg[REG_ADDR_PWD], sizeof(uint16_t)*(REG_ADDR_CNN_MODE - REG_ADDR_PWD));  
    validate_wapi_info(wapi_info);
    save_user_data();
    /* lock REG_ADDR_CERT_STATE to prevent modification */
    reg[REG_ADDR_CERT_STATE] = g_user_save_info.wapi_info.is_exist_certicate;
} 

static void synchro_user_data_2_holding_reg(void)
{
    wapi_info_t *const wapi_info = &g_user_save_info.wapi_info;  
    /* synchronize part 0 */
    mb_holding_reg_part_t *mb_holding_reg_part = &g_mb_holding_reg_part[SERVER_PART];
    uint16_t *reg = mb_holding_reg_part->reg_addr - mb_holding_reg_part->reg_start_index;
    for(uint8_t i=0; i<4; i++)
    {
        memcpy(&reg[REG_ADDR_TARGET_IP_0 + i], &wapi_info->server_ip[i], sizeof(uint8_t));
    }
    memcpy(&reg[REG_ADDR_TARGET_NET_PORT], &wapi_info->server_port, sizeof(uint16_t)); 
    /* synchronize part 2 */  
    mb_holding_reg_part = &g_mb_holding_reg_part[LOCAL_CONFIG_PART];
    reg = mb_holding_reg_part->reg_addr - mb_holding_reg_part->reg_start_index;
    for(uint8_t i=0; i<4; i++)
    {
        memcpy(&reg[REG_ADDR_LOCAL_IP_0 + i], &wapi_info->local_ip[i] , sizeof(uint8_t));
        memcpy(&reg[REG_ADDR_GATEWAY_0 + i], &wapi_info->local_gateway[i], sizeof(uint16_t)); 
    }
    memcpy(&reg[REG_ADDR_SSID], &wapi_info->ssid, sizeof(uint16_t)*(REG_ADDR_PWD-REG_ADDR_SSID));
    memcpy(&reg[REG_ADDR_PWD], &wapi_info->pwd, sizeof(uint16_t)*(REG_ADDR_CNN_MODE-REG_ADDR_PWD));
}

static void synchro_read_only_info_2_holding_reg(void)
{
    mb_holding_reg_part_t *mb_holding_reg_part = &g_mb_holding_reg_part[READ_ONLY_PART];
    uint16_t *reg = mb_holding_reg_part->reg_addr - mb_holding_reg_part->reg_start_index;

    reg[REG_ADDR_SW_VERSION] = SW_VERSION;
    reg[REG_ADDR_HW_VERSION] = HW_VERSION;

    /* __DATE__ format: "Mmm dd yyyy" (e.g. "Jul 21 2026") */
    const char *date = __DATE__;
    /* __TIME__ format: "hh:mm:ss" (e.g. "09:30:15") */
    const char *time = __TIME__;

    /* parse year: skip "Mmm dd " (7 chars), then 4-digit year */
    uint16_t year = (uint16_t)((date[7] - '0') * 1000 + (date[8] - '0') * 100 +
                               (date[9] - '0') * 10 + (date[10] - '0'));
    reg[REG_ADDR_BUILD_YEAR] = year;

    /* parse month from three-letter abbreviation */
    static const char month_strs[12][4] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    uint16_t month = 1;
    for (uint16_t i = 0; i < 12; i++)
    {
        if (date[0] == month_strs[i][0] && date[1] == month_strs[i][1] &&
            date[2] == month_strs[i][2])
        {
            month = i + 1;
            break;
        }
    }
    reg[REG_ADDR_BUILD_MONTH] = month;

    /* parse day: "dd", may be space-padded (e.g. " 1") */
    uint16_t day = (uint16_t)((date[4] == ' ' ? 0 : (date[4] - '0')) * 10 +
                              (date[5] - '0'));
    reg[REG_ADDR_BUILD_DAY] = day;

    /* parse hh:mm:ss */
    uint16_t hour = (uint16_t)((time[0] - '0') * 10 + (time[1] - '0'));
    uint16_t min  = (uint16_t)((time[3] - '0') * 10 + (time[4] - '0'));
    uint16_t sec  = (uint16_t)((time[6] - '0') * 10 + (time[7] - '0'));
    reg[REG_ADDR_BUILD_HOUR] = hour;
    reg[REG_ADDR_BUILD_MIN]  = min;
    reg[REG_ADDR_BUILD_SEC]  = sec;
}

void upper_commu_init(void)
{   
    g_user_save_info.storage_ops = stroage_factory_bulid(ON_CHIP_FLASH_1);
    g_cert_file_info.storage_ops = stroage_factory_bulid(ON_CHIP_FLASH_0);
    recover_user_data();
    update_cert_state();
    // save_user_data();
    synchro_user_data_2_holding_reg();
    synchro_read_only_info_2_holding_reg();
#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
    uart_proto_status_t uart_proto_status = uart_proto_inst(&upp_uart_proto_instance,\
                                                             &upp_uart_proto_input_arg);
    if(UART_PROTO_OK != uart_proto_status)
    {       
        UP_COMMU_DEBUG_OUT("up uart err!\r\n");
        return;
    }
    int ret = agile_modbus_rtu_init(&g_mb_slave_rtu, mb_tx_buf, sizeof(mb_tx_buf),\
                                    upp_frame_parse_att.recv_buf_att->recv_buf,\
                                    upp_frame_parse_att.recv_buf_att->buffer_size);
#else
    // wapi_config_init(mb_slave_parse_filter);
    int ret = agile_modbus_rtu_init(&g_mb_slave_rtu, mb_tx_buf, sizeof(mb_tx_buf),\
                                    g_wapi_uart_rx_buf.recv_buf,\
                                    g_wapi_uart_rx_buf.buffer_size);
#endif
    if(0 != ret)
    {       
        UP_COMMU_DEBUG_OUT("up mb err!\r\n");
        return;
    }            
    agile_modbus_set_slave(&g_mb_slave_rtu._ctx, 1); 

    upper_com_pri_data.mb_handle = &g_mb_slave_rtu;
#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
    upper_com_pri_data.uart_handle = &upp_uart_proto_instance;
#endif
    upper_com_pri_data.if_init = true;
}

#if WAPI_CFG_MODE == WAPI_CFG_BY_WIRED
void notify_upp_commu_recv_isr_cb(void)
{
    if(!upper_com_pri_data.if_init)
        return;
    notify_isr_cb(upper_com_pri_data.uart_handle);
}

void reset_upp_uart(void)
{
    reset_rx_state(&upp_uart_proto_instance);
}
#else
void wapi_config(void)
{
    wapi_config_init(mb_slave_parse_filter);
}
#endif

wapi_info_t *get_wapi_info(void)
{
    if(upper_com_pri_data.if_init)
        return  &g_user_save_info.wapi_info;
    else
        UP_COMMU_DEBUG_OUT("upper communicate not init!\r\n");    
    return NULL;
}

wapi_info_t *get_wapi_info_default(void)
{
    if(upper_com_pri_data.if_init)
        return  &g_user_save_info.wapi_info;
    else
        UP_COMMU_DEBUG_OUT("upper communicate not init!\r\n");    
    return NULL;
}

cert_file_t *get_cert_file(void)
{
    if(upper_com_pri_data.if_init)
        return &g_cert_file_info.cert_file; 
    else
        UP_COMMU_DEBUG_OUT("upper communicate not init!\r\n");    
    return NULL;       
}



wapi_info_t *get_wapi_cfg_info(void)
{
    if(upper_com_pri_data.if_init)
    {
        g_config_wapi_info.is_exist_certicate = g_user_save_info.wapi_info.is_exist_certicate;
        uint16_t digest = M0804C_DATA_INTEGRITY_ALGO((uint8_t *)&g_config_wapi_info,\
                                                 sizeof(wapi_info_t) - sizeof(uint16_t));
        g_config_wapi_info.digest = digest;
        return (wapi_info_t *)&g_config_wapi_info;
    }
    else
        UP_COMMU_DEBUG_OUT("upper communicate not init!\r\n");
    return NULL;

}

void upper_commu_enter_low_power(void)
{
    /* No-op: Modbus state is retained across Stop 2. */
}

void upper_commu_exit_low_power(void)
{
    /* No-op: Modbus state is retained across Stop 2. */
}