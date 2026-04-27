#include "bsp_bluetooth.h"
#include <string.h>

/* =========== 内部变量 =========== */
static UART_HandleTypeDef *bt_huart;

/* DMA双缓冲接收 */
static uint8_t dma_rx_buf[256];

/* 环形缓冲 */
static uint8_t ring_buf[BT_RX_BUF_SIZE];
static volatile uint16_t ring_head = 0;   /* 写指针 */
static volatile uint16_t ring_tail = 0;   /* 读指针 */

/* =========== 内部函数 =========== */
static uint16_t ring_count(void)
{
    return (ring_head - ring_tail + BT_RX_BUF_SIZE) % BT_RX_BUF_SIZE;
}

static void ring_write(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        ring_buf[ring_head] = data[i];
        ring_head = (ring_head + 1) % BT_RX_BUF_SIZE;
        /* 如果写指针追上读指针，丢弃最旧数据 */
        if (ring_head == ring_tail) {
            ring_tail = (ring_tail + 1) % BT_RX_BUF_SIZE;
        }
    }
}

/* =========== 外部接口 =========== */

void BT_Init(UART_HandleTypeDef *huart)
{
    bt_huart = huart;
    ring_head = 0;
    ring_tail = 0;

    /* 启用UART空闲中断 + DMA接收 */
    __HAL_UART_ENABLE_IT(bt_huart, UART_IT_IDLE);
    HAL_UART_Receive_DMA(bt_huart, dma_rx_buf, sizeof(dma_rx_buf));
}

/**
 * @brief 在UART空闲中断中调用，把DMA已接收的数据搬入环形缓冲
 */
void BT_UART_IdleCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != bt_huart->Instance) return;

    /* 计算DMA已接收的字节数 */
    uint16_t dma_remaining = __HAL_DMA_GET_COUNTER(huart->hdmarx);
    uint16_t rx_len = sizeof(dma_rx_buf) - dma_remaining;

    if (rx_len > 0) {
        ring_write(dma_rx_buf, rx_len);
    }

    /* 重启DMA接收 */
    HAL_UART_AbortReceive(huart);
    HAL_UART_Receive_DMA(huart, dma_rx_buf, sizeof(dma_rx_buf));
}

uint16_t BT_Available(void)
{
    return ring_count();
}

uint16_t BT_GetFrame(uint8_t *dest, uint16_t max_len)
{
    uint16_t avail = ring_count();
    uint16_t to_read = (avail < max_len) ? avail : max_len;

    for (uint16_t i = 0; i < to_read; i++) {
        dest[i] = ring_buf[ring_tail];
        ring_tail = (ring_tail + 1) % BT_RX_BUF_SIZE;
    }
    return to_read;
}
