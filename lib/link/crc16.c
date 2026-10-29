#include "link/crc16.h"

#define CRC16_POLY    0x1021U
#define CRC16_TOP_BIT 0x8000U

uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    uint16_t value = crc;

    /* Bitwise implementation: small (no 512-byte table) and fast enough for
     * the data rates involved. */
    for (size_t i = 0U; i < len; i++) {
        value ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            if ((value & CRC16_TOP_BIT) != 0U) {
                value = (uint16_t)((uint16_t)(value << 1U) ^ CRC16_POLY);
            } else {
                value = (uint16_t)(value << 1U);
            }
        }
    }
    return value;
}

uint16_t crc16(const uint8_t *data, size_t len)
{
    return crc16_update(CRC16_INIT, data, len);
}
