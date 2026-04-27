#include "bsp_uart.h"
#include "stm32f1xx_hal.h"
#include <string.h>

#define UART_RX_RING_SIZE 256
#define UART_LINE_BUF_SIZE 128

static UART_HandleTypeDef huart1;
static uint8_t uart_rx_byte;
static volatile uint8_t uart_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t uart_rx_head = 0;
static volatile uint16_t uart_rx_tail = 0;

static void BSP_UART_StartReceiveIT(void)
{
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
}

static void BSP_UART_PushByte(uint8_t byte)
{
    uint16_t next = (uint16_t)((uart_rx_head + 1U) % UART_RX_RING_SIZE);
    if (next == uart_rx_tail) {
        uart_rx_tail = (uint16_t)((uart_rx_tail + 1U) % UART_RX_RING_SIZE);
    }
    uart_rx_ring[uart_rx_head] = byte;
    uart_rx_head = next;
}

static uint8_t BSP_UART_PopByte(uint8_t *byte)
{
    if (uart_rx_head == uart_rx_tail) {
        return 0;
    }

    *byte = uart_rx_ring[uart_rx_tail];
    uart_rx_tail = (uint16_t)((uart_rx_tail + 1U) % UART_RX_RING_SIZE);
    return 1;
}

static void BSP_UART_SendUInt(uint16_t val)
{
    char buf[6];
    char rev[5];
    uint8_t len = 0;

    do {
        rev[len++] = (char)('0' + (val % 10U));
        val /= 10U;
    } while (val > 0U && len < sizeof(rev));

    for (uint8_t i = 0; i < len; i++) {
        buf[i] = rev[len - 1U - i];
    }
    buf[len] = '\0';
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 10);
}

void BSP_UART_Init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9=TX (AF推挽), PA10=RX (�?空输�?) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart1);

    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    uart_rx_head = 0;
    uart_rx_tail = 0;
    BSP_UART_StartReceiveIT();
}

void BSP_UART_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void BSP_UART_SendString(const char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)str, (uint16_t)strlen(str), 100);
}

void BSP_UART_SendSpectrum(const uint16_t *mag, uint16_t len)
{
    BSP_UART_SendString("FFT:");
    for (uint16_t i = 0; i < len; i++) {
        BSP_UART_SendUInt(mag[i]);
        if (i < len - 1) {
            HAL_UART_Transmit(&huart1, (uint8_t *)",", 1, 5);
        }
    }
    BSP_UART_SendString("\r\n");
}

uint8_t BSP_UART_ReadLine(char *line, uint16_t max_len)
{
    static char line_buf[UART_LINE_BUF_SIZE];
    static uint16_t line_len = 0;
    uint8_t byte = 0;

    if (line == NULL || max_len == 0) {
        return 0;
    }

    while (BSP_UART_PopByte(&byte)) {
        if (byte == '\r') {
            continue;
        }

        if (byte == '\n') {
            uint16_t copy_len = line_len;
            if (copy_len >= max_len) {
                copy_len = (uint16_t)(max_len - 1U);
            }
            memcpy(line, line_buf, copy_len);
            line[copy_len] = '\0';
            line_len = 0;
            return 1;
        }

        if (line_len < (UART_LINE_BUF_SIZE - 1U)) {
            line_buf[line_len++] = (char)byte;
        } else {
            line_len = 0;
        }
    }

    return 0;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        BSP_UART_PushByte(uart_rx_byte);
        BSP_UART_StartReceiveIT();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        BSP_UART_StartReceiveIT();
    }
}
