# Serial console: interrupts, DMA and ring buffers

## In short

The board talks to the laptop over a serial port at 1.5 million bits per second. A simple
implementation would make the processor wait while each character goes out: about 7 microseconds
per character, or 0.7 milliseconds for a 100-character line. That is too long for a device that
also has to catch CAN messages arriving every 250 microseconds or so.

This firmware never waits for the serial port:

- **Receiving:** each incoming character interrupts the processor just long enough to store the
  character in a buffer. The main program reads from that buffer when it is ready.
- **Sending:** the main program drops text into a second buffer and carries on. The chip's
  **DMA controller**, a small hardware unit that copies memory on its own, feeds the characters to
  the serial port in the background. When it finishes a block, it starts the next one by itself.

The buffers are **lock-free ring buffers**: one side only adds data, the other only removes it,
so they can be shared between an interrupt and the main program without switching interrupts off.
That code is unit-tested on a computer, including a two-thread stress test checked by
ThreadSanitizer.

| Term | Meaning |
|---|---|
| Interrupt | A hardware signal that makes the processor pause, run a short handler, and resume |
| DMA | Direct Memory Access: hardware that moves data without the processor |
| Ring buffer | A fixed-size queue where the write and read positions wrap around at the end |
| Critical section | A few instructions run with interrupts briefly disabled, so they can't be interleaved with an interrupt handler |

## Data flow

```
                 USART2 RX interrupt                    main loop
 laptop  --->  [ store byte ] ---> rx ring (1024 B) --> console_poll() ---> console core ---> commands
                                                                                 |
 laptop  <---  USART2 <--- DMA1 stream 6 <--- tx ring (4096 B) <--- uart_link_write() <-----+
                              |
                              +--> transfer-complete interrupt: release sent bytes, start next block
```

## Engineering detail

### Receive path (`USART2_IRQHandler`)

- Interrupt on RXNE (receive register not empty), priority 5.
- Reads `SR` then `DR`. That sequence also clears the overrun (ORE), framing (FE) and noise (NE)
  flags, so the handler can't get stuck re-entering on an error.
- Bytes with framing or noise errors are discarded and counted. Overruns (a byte lost because the
  previous one wasn't read in time) are counted. If the ring is full, the byte is dropped and
  counted.
- The handler does nothing else: no parsing, no formatting. It runs in a few dozen cycles.

### Transmit path (DMA1 stream 6, channel 4)

- `uart_link_write()` copies data into the transmit ring (the producer side) and then "kicks" the
  DMA. If the DMA is idle, the kick starts a transfer of the next **contiguous** block from the
  ring, using `ring_peek_contiguous()`, so no intermediate copy is needed. Blocks are at most
  256 bytes. Space in the ring is only released when a transfer completes, so short transfers
  free it in small steps. A transfer of the whole 4 KB ring would hold its space for 27 ms, or
  over 300 ms at slower speeds, and a writer waiting for space could give up first.
- On transfer complete, the DMA interrupt (priority 6) releases exactly the bytes that were sent
  (`ring_consume()`) and immediately starts the next block, so a long message streams out as a
  chain of transfers with no processor involvement in between.
- **Race avoided:** both the main loop (kick) and the DMA interrupt can start a transfer. The
  kick's "is the DMA idle? then start" check runs inside a critical section of a few instructions,
  so the interrupt can't start a transfer between the check and the start. Critical sections save
  and restore the previous interrupt state, so they nest safely.
- **Bytes in flight stay valid:** the producer only ever writes into free space, never into the
  region between the read position and the write position, so the block the DMA is reading can't
  be overwritten.
- **Back-pressure:** if the ring is full, `uart_link_write()` waits up to 50 ms for space, then
  drops the rest and counts it (`uart_tx_dropped`). Output is never allowed to block the firmware
  indefinitely.
- **Flow control for commands:** `console_poll()` runs at most one command per pass of the main
  loop, and only when at least 1 KB of the transmit ring is free, more than the longest reply
  (570 bytes). A reply therefore never waits for space. Commands sent in a burst (pasted into a
  terminal, or from a program that doesn't wait for replies) queue up in the receive ring and run
  one per pass. Without this, a burst ran entirely in one pass: its replies took longer to send
  than the watchdog allows, and the board restarted. A test on the laptop side checks every reply
  fits.
- Transfer errors are counted (`uart_tx_dma_errors`). The block is released and sending
  continues.

### Restart without losing the reply

`reset` doesn't restart the chip inside its handler, because the `OK` reply hasn't been sent yet.
It sets a flag. The main loop then calls `uart_link_flush()`, which waits until the ring is empty,
the DMA is idle, and the UART's transmission-complete flag shows the last bit has left the pin.
Only then does it call `NVIC_SystemReset()`.

### Statistics

All counters are 32-bit words updated in interrupt handlers and read by `status`. A 32-bit read
is atomic on the Cortex-M4, so no locking is needed to report them.

| Counter | Incremented when |
|---|---|
| `uart_rx_bytes` | A byte is stored in the receive ring |
| `uart_rx_dropped` | The receive ring is full |
| `uart_rx_overruns` | Hardware overrun: a byte arrived before the previous one was read |
| `uart_rx_errors` | Framing or noise error (wrong baud rate, bad cable) |
| `uart_tx_bytes` | Bytes confirmed sent by the DMA |
| `uart_tx_dropped` | Output discarded after waiting 50 ms for buffer space |
| `uart_tx_dma_errors` | DMA transfer error |

### Speed

The link runs at **1,500,000 baud**. The board's serial port and the ST-LINK's USB bridge were
stress-tested at every speed the 45 MHz peripheral clock divides exactly. Bursts of commands were
sent and every reply was checked byte for byte:

| Speed (baud) | Divisor error | Sustained throughput | Errors |
|---|---|---|---|
| 115,200 | 0.10% | 11.5 kB/s | 0 |
| 921,600 | 0.35% | 91.5 kB/s | 0 |
| 1,000,000 | 0 | 98.9 kB/s | 0 |
| **1,500,000** | **0** | **148.2 kB/s** | **0** |
| 2,250,000 | 0 | 221.2 kB/s | 0 |

A CAN frame sent to the laptop as a binary packet takes 25 bytes, so 1.5 Mbaud carries about
5,900 frames per second. That is more than a fully loaded 500 kbit/s bus can produce (about 4,500
frames per second with 8 data bytes), so the link is never the bottleneck. 2.25 Mbaud is faster
still, but it is not a standard speed for Linux serial terminals, and 1.5 Mbaud is.

One trap on the laptop side: 1.5 Mbaud is not in the classic list of serial speeds, so on macOS
pyserial applies it in two steps, through a placeholder speed. It does that again whenever any
port setting changes, and the laptop tool used to change the read timeout on every read. Each
time, the ST-LINK briefly switched speed and bytes in flight were lost. Commands went unanswered,
even though a quick manual test worked. The tool now configures the port once and waits for data
by polling how many bytes have arrived (`host/src/obdsniff/transport.py`).

### Sizing

| Buffer | Size | Reasoning |
|---|---|---|
| Receive | 1024 bytes | About 7 ms of continuous input at 1.5 Mbaud; commands are a few bytes each |
| Transmit | 4096 bytes | Several full replies, plus room for streamed data; 1 KB is always kept for command replies |
