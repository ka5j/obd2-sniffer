/*
 * Binary packets from the board to the laptop.
 *
 * In host mode, captured CAN frames go to the laptop as binary packets mixed
 * into the console's text output. Each packet is
 *
 *     0x00 | COBS( type | payload | CRC-16 ) | 0x00
 *
 * COBS removes every zero byte from the packet body, and console text never
 * contains a zero byte, so the zeros mark exactly where each packet starts
 * and ends. The CRC-16/CCITT-FALSE (crc16.h) covers type and payload; all
 * multi-byte fields are little-endian. The full format is in
 * docs/protocol.md.
 */
#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "can/can_frame.h"

#define PACKET_DELIMITER      0x00U
#define PACKET_TYPE_CAN_FRAME 0x01U

/* CAN frame payload: seq u16 (the frame's capture number), timestamp_us u32,
 * id u32, dlc u8, data. */
#define PACKET_CAN_ID_EXTENDED 0x80000000U /* id bit 31: 29-bit identifier */
#define PACKET_CAN_ID_RTR      0x40000000U /* id bit 30: remote frame */

/* Bytes on the wire for a CAN frame with 8 data bytes (the largest). */
#define PACKET_CAN_FRAME_MAX 25U

/* Write the packet for one CAN frame, delimiters included. A remote frame
 * carries no data bytes. Returns the number of bytes written, or 0 if the
 * frame is invalid (DLC above 8) or `size` is too small. */
size_t packet_encode_can_frame(const can_frame_t *frame, uint8_t *out, size_t size);

#endif /* PACKET_H */
