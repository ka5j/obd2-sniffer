#include "crc16.h"

uint16_t crc16_update(uint16_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;

    /* Bitwise implementation: small (no 512-byte table) and fast enough for
     * the data rates involved. */
    while (len-- > 0U) {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t crc16(const void *data, size_t len)
{
    return crc16_update(CRC16_INIT, data, len);
}
