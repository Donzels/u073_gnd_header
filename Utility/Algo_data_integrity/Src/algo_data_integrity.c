#include "algo_data_integrity.h"

uint16_t checksum_16bit(uint8_t *const buf, uint16_t len)
{
    uint16_t sum = 0;
    for (uint16_t i=0; i<len ; i++) 
    {
        sum += buf[i];
    }
    return ~sum;
}
