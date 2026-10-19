/*
 * A CAN frame as one line of text, for watching traffic in a serial
 * terminal:
 *
 *     12.345678  7E8       [8]  04 41 0C 1A F8 00 00 00
 *     12.345901  18DAF110  [3]  02 10 03
 *     12.346120  7DF       [8]  remote request
 *
 * Receive time in seconds with microseconds, the identifier in hex (3 digits
 * standard, 8 extended), the data length in brackets, then the data bytes.
 * Written by hand rather than with printf: it runs for every frame.
 */
#ifndef CAN_TEXT_H
#define CAN_TEXT_H

#include <stddef.h>

#include "can_frame.h"

/* Longest line, including "\r\n" and the terminating NUL. */
#define CAN_TEXT_MAX 64U

/* Write the line for `frame`, ending in "\r\n", into `out` (NUL-terminated).
 * Returns its length without the NUL, or 0 if `size` is below CAN_TEXT_MAX
 * or the DLC is above 8. */
size_t can_text_format(const can_frame_t *frame, char *out, size_t size);

#endif /* CAN_TEXT_H */
