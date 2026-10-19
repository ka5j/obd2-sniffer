/*
 * CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no bit
 * reflection, no final XOR. Check value: crc16("123456789") == 0x29B1.
 *
 * Used to detect corrupted data: crash records kept in RAM across resets,
 * and packets on the serial link.
 */
#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_INIT 0xFFFFU

/* CRC of a whole buffer. */
uint16_t crc16(const uint8_t *data, size_t len);

/* Continue a CRC over more data: crc16_update(CRC16_INIT, ...) for the first
 * chunk, then feed each result into the next call. */
uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len);

#endif /* CRC16_H */
