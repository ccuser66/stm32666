#include "bsp_led_bar.h"
#include "stm32f1xx_hal.h"

/* LED0=PA15, LED1~LED7=PB3~PB9 (低频→高频) */
static GPIO_TypeDef* const led_ports[LED_BAR_COUNT] = {
    GPIOA, GPIOB, GPIOB, GPIOB,
    GPIOB, GPIOB, GPIOB, GPIOB
};
static const uint16_t led_pins[LED_BAR_COUNT] = {
    GPIO_PIN_15, GPIO_PIN_3, GPIO_PIN_4, GPIO_PIN_5,
    GPIO_PIN_6,  GPIO_PIN_7, GPIO_PIN_8, GPIO_PIN_9
};

void LedBar_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    for (uint8_t i = 0; i < LED_BAR_COUNT; i++) {
        gpio.Pin = led_pins[i];
        HAL_GPIO_Init(led_ports[i], &gpio);
    }

    LedBar_AllOff();
}

void LedBar_AllOff(void)
{
    for (uint8_t i = 0; i < LED_BAR_COUNT; i++) {
        HAL_GPIO_WritePin(led_ports[i], led_pins[i], GPIO_PIN_RESET);
    }
}

void LedBar_Update(const uint16_t *mag, uint16_t num_bins)
{
    if (num_bins == 0) return;

    /* 将频率bin均分为8段 */
    uint16_t bins_per_led = num_bins / LED_BAR_COUNT;
    if (bins_per_led == 0) bins_per_led = 1;

    /* 阈值: 幅度超过此值则点亮，可后续用电位器调节 */
    const uint16_t threshold = 20;

    for (uint8_t i = 0; i < LED_BAR_COUNT; i++) {
        uint32_t sum = 0;
        uint16_t start = i * bins_per_led;
        uint16_t end   = start + bins_per_led;
        if (end > num_bins) end = num_bins;

        /* 第一组跳过DC分量(bin0) */
        uint16_t b_start = start;
        if (i == 0 && b_start == 0) b_start = 1;

        uint16_t count = 0;
        for (uint16_t b = b_start; b < end; b++) {
            sum += mag[b];
            count++;
        }
        uint16_t avg = (count > 0) ? (uint16_t)(sum / count) : 0;

        HAL_GPIO_WritePin(led_ports[i], led_pins[i],
                          (avg > threshold) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}
