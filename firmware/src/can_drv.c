#include "can_drv.h"

#include <string.h>

#include "board.h"
#include "can_timing.h"
#include "frame_queue.h"
#include "timebase.h"

/* Highest priority in the system: frames are timestamped as soon as they
 * arrive, and the 3-frame hardware FIFO is emptied before it can overflow. */
#define CAN_RX_IRQ_PRIORITY 1U

/* 8 KiB: about 400 frames, roughly 100 ms of a fully loaded bus. */
#define RX_QUEUE_BYTES 8192U

/* CAN2 shares CAN1's filter banks; banks from here on are CAN2's. */
#define CAN2_FIRST_FILTER_BANK 14U

void CAN1_RX0_IRQHandler(void);

static CAN_HandleTypeDef hcan1;
static frame_queue_t rx_queue;
static can_drv_mode_t mode = CAN_DRV_OFF;
static uint32_t bitrate_bps;

/* Written by the receive interrupt, read by the main loop: each counter is a
 * single 32-bit word, read atomically. */
static volatile uint32_t rx_frames;
static volatile uint32_t rx_dropped;
static volatile uint32_t rx_overruns;
static volatile uint32_t isr_max_cycles;
static uint32_t tx_frames;
static uint16_t capture_seq; /* receive interrupt only */

/* ---- receive interrupt -------------------------------------------------- */

static void read_frame(const CAN_RxHeaderTypeDef *header, const uint8_t *data, can_frame_t *f)
{
    f->timestamp_us = timebase_us();
    f->flags = 0U;
    if (header->IDE == CAN_ID_EXT) {
        f->id = header->ExtId;
        f->flags |= CAN_FLAG_EXTENDED;
    } else {
        f->id = header->StdId;
    }
    if (header->RTR == CAN_RTR_REMOTE) {
        f->flags |= CAN_FLAG_RTR;
    }
    f->dlc = (uint8_t)header->DLC;
    for (uint32_t i = 0U; i < CAN_MAX_DLC; i++) {
        f->data[i] = data[i];
    }
    /* Counts every frame, even one about to be dropped, so the laptop sees
     * the gap. */
    f->seq = capture_seq;
    capture_seq++;
}

void CAN1_RX0_IRQHandler(void)
{
    uint32_t start = timebase_cycles();
    CAN_RxHeaderTypeDef header;
    uint8_t data[CAN_MAX_DLC];
    can_frame_t frame;

    (void)memset(data, 0, sizeof data);

    while (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) > 0U) {
        if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &header, data) != HAL_OK) {
            break;
        }
        read_frame(&header, data, &frame);
        if (frame_queue_push(&rx_queue, &frame)) {
            rx_frames++;
        } else {
            rx_dropped++;
        }
    }
    /* A frame arrived while the FIFO was full: the controller lost it.
     * The flag is cleared by writing 1. */
    if ((CAN1->RF0R & CAN_RF0R_FOVR0) != 0U) {
        rx_overruns++;
        CAN1->RF0R = CAN_RF0R_FOVR0;
    }

    uint32_t cycles = timebase_cycles() - start;
    if (cycles > isr_max_cycles) {
        isr_max_cycles = cycles;
    }
}

/* ---- set-up ---------------------------------------------------------------- */

/* Pins and clock. Called by HAL_CAN_Init(); the signature is fixed by the
 * HAL, which declares it weak. */
// cppcheck-suppress constParameterPointer
void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance != CAN1) {
        return;
    }
    __HAL_RCC_CAN1_CLK_ENABLE();  /* MISRA DEV-02 */
    __HAL_RCC_GPIOB_CLK_ENABLE(); /* MISRA DEV-02 */

    GPIO_InitTypeDef pin = {
        .Pin = BOARD_CAN1_RX_PIN,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_PULLUP, /* recessive (idle) level if nothing is connected */
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = BOARD_CAN1_AF,
    };
    HAL_GPIO_Init(BOARD_CAN1_PORT, &pin);
    pin.Pin = BOARD_CAN1_TX_PIN;
    pin.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BOARD_CAN1_PORT, &pin);
}

bool can_drv_init(void)
{
    static uint8_t rx_storage[RX_QUEUE_BYTES];

    if (!frame_queue_init(&rx_queue, rx_storage, RX_QUEUE_BYTES)) {
        return false;
    }
    hcan1.Instance = CAN1;
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, CAN_RX_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    return true;
}

/* Accept every frame into FIFO 0: a sniffer wants all of them. */
static bool accept_all(void)
{
    CAN_FilterTypeDef filter = {
        .FilterIdHigh = 0U,
        .FilterIdLow = 0U,
        .FilterMaskIdHigh = 0U, /* mask of all zeros: no bit has to match */
        .FilterMaskIdLow = 0U,
        .FilterFIFOAssignment = CAN_FILTER_FIFO0,
        .FilterBank = 0U,
        .FilterMode = CAN_FILTERMODE_IDMASK,
        .FilterScale = CAN_FILTERSCALE_32BIT,
        .FilterActivation = CAN_FILTER_ENABLE,
        .SlaveStartFilterBank = CAN2_FIRST_FILTER_BANK,
    };
    return HAL_CAN_ConfigFilter(&hcan1, &filter) == HAL_OK;
}

bool can_drv_start_loopback(uint32_t bitrate)
{
    can_timing_t timing;
    if (!can_timing_calc(HAL_RCC_GetPCLK1Freq(), bitrate, &timing)) {
        return false;
    }
    can_drv_stop();

    hcan1.Init.Prescaler = timing.prescaler;
    hcan1.Init.Mode = CAN_MODE_SILENT_LOOPBACK;
    /* Register fields hold "value - 1" (RM0390 CAN_BTR). */
    hcan1.Init.SyncJumpWidth = (timing.sjw - 1U) << CAN_BTR_SJW_Pos;
    hcan1.Init.TimeSeg1 = (timing.bs1 - 1U) << CAN_BTR_TS1_Pos;
    hcan1.Init.TimeSeg2 = (timing.bs2 - 1U) << CAN_BTR_TS2_Pos;
    hcan1.Init.TimeTriggeredMode = DISABLE;
    hcan1.Init.AutoBusOff = DISABLE; /* recovering from bus-off is a decision, not automatic */
    hcan1.Init.AutoWakeUp = DISABLE;
    hcan1.Init.AutoRetransmission = ENABLE;
    hcan1.Init.ReceiveFifoLocked = DISABLE;
    hcan1.Init.TransmitFifoPriority = ENABLE; /* send in the order queued, not by ID */

    const uint32_t interrupts =
        CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN; /* MISRA DEV-02 */
    if ((HAL_CAN_Init(&hcan1) != HAL_OK) || !accept_all() ||
        (HAL_CAN_ActivateNotification(&hcan1, interrupts) != HAL_OK)) {
        (void)HAL_CAN_DeInit(&hcan1);
        return false;
    }

    /* No interrupt can run yet: CAN hasn't started. */
    capture_seq = 0U;
    rx_frames = 0U;
    rx_dropped = 0U;
    rx_overruns = 0U;
    isr_max_cycles = 0U;
    tx_frames = 0U;

    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        (void)HAL_CAN_DeInit(&hcan1);
        return false;
    }
    mode = CAN_DRV_LOOPBACK;
    bitrate_bps = bitrate;
    return true;
}

void can_drv_stop(void)
{
    if (mode == CAN_DRV_OFF) {
        return;
    }
    /* Resets the controller; frames already queued stay until read. */
    (void)HAL_CAN_DeInit(&hcan1);
    mode = CAN_DRV_OFF;
    bitrate_bps = 0U;
}

can_drv_mode_t can_drv_mode(void)
{
    return mode;
}

const char *can_drv_mode_name(can_drv_mode_t m)
{
    return (m == CAN_DRV_LOOPBACK) ? "loopback" : "off";
}

uint32_t can_drv_bitrate(void)
{
    return bitrate_bps;
}

/* ---- transmit ----------------------------------------------------------------- */

uint32_t can_drv_tx_free(void)
{
    return (mode == CAN_DRV_OFF) ? 0U : HAL_CAN_GetTxMailboxesFreeLevel(&hcan1);
}

bool can_drv_send(const can_frame_t *frame)
{
    if ((mode != CAN_DRV_LOOPBACK) || (frame->dlc > CAN_MAX_DLC) ||
        (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U)) {
        return false;
    }
    CAN_TxHeaderTypeDef header = {
        .DLC = frame->dlc,
        .TransmitGlobalTime = DISABLE,
    };
    if ((frame->flags & CAN_FLAG_EXTENDED) != 0U) {
        header.IDE = CAN_ID_EXT;
        header.ExtId = frame->id;
    } else {
        header.IDE = CAN_ID_STD;
        header.StdId = frame->id;
    }
    header.RTR = ((frame->flags & CAN_FLAG_RTR) != 0U) ? CAN_RTR_REMOTE : CAN_RTR_DATA;

    uint32_t mailbox = 0U;
    if (HAL_CAN_AddTxMessage(&hcan1, &header, frame->data, &mailbox) != HAL_OK) {
        return false;
    }
    tx_frames++;
    return true;
}

/* ---- main loop side ---------------------------------------------------------- */

bool can_drv_receive(can_frame_t *frame)
{
    return frame_queue_pop(&rx_queue, frame);
}

uint32_t can_drv_queued(void)
{
    return frame_queue_count(&rx_queue);
}

uint32_t can_drv_queue_capacity(void)
{
    return frame_queue_capacity(&rx_queue);
}

void can_drv_get_stats(can_drv_stats_t *out)
{
    out->rx_frames = rx_frames;
    out->rx_dropped = rx_dropped;
    out->rx_overruns = rx_overruns;
    out->tx_frames = tx_frames;
    out->isr_max_cycles = isr_max_cycles;
    uint32_t esr = CAN1->ESR;
    out->tx_errors = (esr & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos;
    out->rx_errors = (esr & CAN_ESR_REC) >> CAN_ESR_REC_Pos;
    out->error_passive = (esr & CAN_ESR_EPVF) != 0U;
    out->bus_off = (esr & CAN_ESR_BOFF) != 0U;
}
