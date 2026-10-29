#include "console/uart_link.h"

#include "system/board.h"
#include "system/critical.h"
#include "buffers/ring_buffer.h"

#define RX_BUFFER_SIZE 1024U /* about 7 ms of input at 1.5 Mbaud */
#define TX_BUFFER_SIZE 4096U /* command replies plus streamed CAN frames */
/* Longest single DMA transfer. Space in the transmit buffer is released when
 * a transfer completes, so shorter transfers free it in small steps instead
 * of all at once (a full-buffer transfer would hold its space for 27 ms at
 * 1.5 Mbaud, and for over 300 ms at slower speeds). */
#define TX_DMA_MAX_BLOCK 256U

/* Priorities follow deadlines. The UART holds one received byte, and at
 * 1.5 Mbaud the next one arrives 6.7 us later: if the handler waits longer,
 * a byte is lost (an overrun). Nothing else in the system has a deadline
 * that short, so receiving comes first, ahead of the CAN interrupt, whose
 * 3-frame FIFO can wait about 90 us even on a full 1 Mbit/s bus. The handler
 * takes well under a microsecond. Sending has no deadline: the DMA just
 * moves on to the next block a little later. */
#define UART_IRQ_PRIORITY 0U
#define DMA_IRQ_PRIORITY  6U

/* USART2_TX is DMA1 stream 6, channel 4 (RM0390 table 28). */
#define TX_DMA_STREAM    DMA1_Stream6
#define TX_DMA_IRQN      DMA1_Stream6_IRQn
#define TX_DMA_CHANNEL_4 DMA_SxCR_CHSEL_2
#define TX_DMA_FLAGS                                                                               \
    (DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6)

void USART2_IRQHandler(void);
void DMA1_Stream6_IRQHandler(void);

static ring_buffer_t rx_ring;
static ring_buffer_t tx_ring;

/* Bytes the DMA is currently sending (0 = idle). Written by the main loop
 * inside a critical section and by the DMA interrupt. */
static volatile size_t tx_in_flight;

static volatile uart_link_stats_t stats;

/* ---- transmit ------------------------------------------------------------ */

/* Start a DMA transfer of the next contiguous block, if there is one. Called
 * from the DMA interrupt or with interrupts disabled. */
static void tx_start_next(void)
{
    const uint8_t *data = NULL;
    size_t count = ring_peek_contiguous(&tx_ring, &data);
    if (count == 0U) {
        return;
    }
    if (count > TX_DMA_MAX_BLOCK) {
        count = TX_DMA_MAX_BLOCK;
    }
    tx_in_flight = count;
    /* Clear "transmission complete" so it only becomes set again once this
     * block's last bit has left the pin (RM0390, USART chapter: transmission
     * using DMA).
     * TC is cleared by writing 0; writing 1 to the other flags has no
     * effect, so no read-modify-write is needed. */
    USART2->SR = ~USART_SR_TC;
    DMA1->HIFCR = TX_DMA_FLAGS;
    TX_DMA_STREAM->M0AR = (uint32_t)data;
    TX_DMA_STREAM->NDTR = (uint32_t)count;
    TX_DMA_STREAM->CR |= DMA_SxCR_EN;
}

/* Start sending if the DMA is idle. Safe to call from the main loop. */
static void tx_kick(void)
{
    uint32_t state = critical_enter();
    if (tx_in_flight == 0U) {
        tx_start_next();
    }
    critical_exit(state);
}

void DMA1_Stream6_IRQHandler(void)
{
    uint32_t status = DMA1->HISR;
    DMA1->HIFCR = TX_DMA_FLAGS;

    if ((status & DMA_HISR_TEIF6) != 0U) {
        stats.tx_dma_errors++; /* block abandoned: not counted as sent */
    } else if ((status & DMA_HISR_TCIF6) != 0U) {
        stats.tx_bytes += (uint32_t)tx_in_flight;
    } else {
        return; /* no transfer finished (spurious or FIFO flag only) */
    }
    /* Either way the block is finished: release it and send the next one. */
    ring_consume(&tx_ring, tx_in_flight);
    tx_in_flight = 0U;
    tx_start_next();
}

size_t uart_link_write(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    size_t done = 0U;
    uint32_t start = HAL_GetTick();

    while (done < len) {
        done += ring_write(&tx_ring, &data[done], len - done);
        tx_kick();
        if ((done < len) && ((HAL_GetTick() - start) >= timeout_ms)) {
            stats.tx_dropped += (uint32_t)(len - done);
            break;
        }
    }
    return done;
}

bool uart_link_flush(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    /* Done when nothing is queued, the DMA is idle, and the last byte has
     * been shifted out of the UART (transmission complete flag). */
    while (!ring_is_empty(&tx_ring) || (tx_in_flight != 0U) || ((USART2->SR & USART_SR_TC) == 0U)) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return false;
        }
    }
    return true;
}

/* ---- receive ------------------------------------------------------------- */

void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;
    const uint32_t rx_flags = USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE;

    if ((status & rx_flags) == 0U) {
        return;
    }
    /* Reading DR after SR clears all of these flags. */
    uint8_t byte = (uint8_t)(USART2->DR & 0xFFU);

    if ((status & USART_SR_ORE) != 0U) {
        stats.rx_overruns++;
    }
    if ((status & (USART_SR_FE | USART_SR_NE)) != 0U) {
        stats.rx_errors++; /* corrupted byte: discard */
    } else if (ring_put(&rx_ring, byte)) {
        stats.rx_bytes++;
    } else {
        stats.rx_dropped++;
    }
}

bool uart_link_read(uint8_t *byte)
{
    return ring_get(&rx_ring, byte);
}

/* ---- set-up -------------------------------------------------------------- */

/* Pins and peripheral clock. Called by HAL_UART_Init(); the signature is
 * fixed by the HAL, which declares it weak. */
// cppcheck-suppress constParameterPointer
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }

    __HAL_RCC_USART2_CLK_ENABLE(); /* MISRA DEV-02 */
    __HAL_RCC_GPIOA_CLK_ENABLE();  /* MISRA DEV-02 */

    GPIO_InitTypeDef pin = {
        .Pin = BOARD_VCP_TX_PIN,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_PULLUP, /* idle-high line stays defined if ST-LINK is unpowered */
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = BOARD_VCP_AF,
    };
    HAL_GPIO_Init(BOARD_VCP_TX_PORT, &pin);

    pin.Pin = BOARD_VCP_RX_PIN;
    HAL_GPIO_Init(BOARD_VCP_RX_PORT, &pin);
}

static void tx_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE(); /* MISRA DEV-02 */

    /* The stream must be disabled before it can be configured. */
    TX_DMA_STREAM->CR = 0U;
    while ((TX_DMA_STREAM->CR & DMA_SxCR_EN) != 0U) {
    }
    TX_DMA_STREAM->PAR = (uint32_t)&USART2->DR;
    TX_DMA_STREAM->FCR = 0U; /* direct mode, no FIFO */
    /* Memory to peripheral, byte transfers, memory address increments,
     * interrupts on transfer complete and transfer error. */
    TX_DMA_STREAM->CR =
        TX_DMA_CHANNEL_4 | DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_TCIE | DMA_SxCR_TEIE;
    DMA1->HIFCR = TX_DMA_FLAGS;

    HAL_NVIC_SetPriority(TX_DMA_IRQN, DMA_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(TX_DMA_IRQN);
}

bool uart_link_init(void)
{
    /* Only used here, so declared here (MISRA Rule 8.9); static so the
     * storage outlives the call. */
    static UART_HandleTypeDef huart2;
    static uint8_t rx_storage[RX_BUFFER_SIZE];
    static uint8_t tx_storage[TX_BUFFER_SIZE];

    if (!ring_init(&rx_ring, rx_storage, RX_BUFFER_SIZE) ||
        !ring_init(&tx_ring, tx_storage, TX_BUFFER_SIZE)) {
        return false;
    }

    huart2.Instance = USART2;
    huart2.Init.BaudRate = UART_LINK_BAUD;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX; /* MISRA DEV-02 */
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        return false;
    }

    tx_dma_init();
    USART2->CR3 |= USART_CR3_DMAT;

    HAL_NVIC_SetPriority(USART2_IRQn, UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    USART2->CR1 |= USART_CR1_RXNEIE;
    return true;
}

size_t uart_link_tx_free(void)
{
    return (size_t)ring_free(&tx_ring);
}

void uart_link_get_stats(uart_link_stats_t *out)
{
    /* Each counter is a single 32-bit word, read atomically on Cortex-M4. */
    out->rx_bytes = stats.rx_bytes;
    out->rx_dropped = stats.rx_dropped;
    out->rx_overruns = stats.rx_overruns;
    out->rx_errors = stats.rx_errors;
    out->tx_bytes = stats.tx_bytes;
    out->tx_dropped = stats.tx_dropped;
    out->tx_dma_errors = stats.tx_dma_errors;
}
