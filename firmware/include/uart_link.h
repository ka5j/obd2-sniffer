/*
 * Serial link to the laptop over USART2 (ST-LINK virtual COM port).
 *
 * Receive: each byte raises an interrupt and is stored in a lock-free ring
 * buffer; the main loop reads it out with uart_link_read().
 *
 * Transmit: uart_link_write() copies data into a second ring buffer and
 * returns. DMA moves the bytes to the UART in the background, and the DMA
 * completion interrupt starts the next chunk, so the CPU never waits for
 * the serial port.
 *
 * uart_link_write() and uart_link_flush() must be called from the main loop
 * only (not from interrupts or with interrupts disabled), because they wait
 * on the system tick when the transmit buffer is full.
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UART_LINK_BAUD 115200U

typedef struct {
    uint32_t rx_bytes;      /* bytes received and stored */
    uint32_t rx_dropped;    /* received while the receive buffer was full */
    uint32_t rx_overruns;   /* bytes lost in hardware: interrupt served too late */
    uint32_t rx_errors;     /* framing or noise errors (wrong baud rate, bad cable) */
    uint32_t tx_bytes;      /* bytes sent */
    uint32_t tx_dropped;    /* discarded because the transmit buffer stayed full */
    uint32_t tx_dma_errors; /* DMA transfer errors */
} uart_link_stats_t;

/* Configure USART2, its pins, DMA and interrupts. */
bool uart_link_init(void);

/* Next received byte, if any. */
bool uart_link_read(uint8_t *byte);

/* Queue `len` bytes for sending. Waits up to `timeout_ms` for buffer space;
 * anything that still doesn't fit is dropped and counted. Returns the
 * number of bytes queued. */
size_t uart_link_write(const uint8_t *data, size_t len, uint32_t timeout_ms);

/* Wait until everything queued has left the UART. False on timeout. */
bool uart_link_flush(uint32_t timeout_ms);

/* Bytes that can be queued right now without waiting. */
size_t uart_link_tx_free(void);

void uart_link_get_stats(uart_link_stats_t *out);

#endif /* UART_LINK_H */
