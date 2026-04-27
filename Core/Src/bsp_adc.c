#include "bsp_adc.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/* ===== �?件资�? =====
 * ADC1 + DMA1_Channel1 + TIM3: 音�??8kHz采样
 *   - CH7 (PA7): 3.5mm音�?�输�?
 *   - CH8 (PB0): MAX9814麦克�?
 * ADC2 (�?件触�?): 电位�?
 *   - CH9 (PB1): 电位�?
 */

#define ADC_BUF_SIZE  256

static ADC_HandleTypeDef hadc1;
static ADC_HandleTypeDef hadc2;
static DMA_HandleTypeDef hdma_adc1;
static TIM_HandleTypeDef htim3;

static volatile uint16_t adc_raw[ADC_BUF_SIZE];
static volatile uint8_t  frame_ready = 0;
static AudioSource_t     current_src = AUDIO_SRC_MIC;
static uint8_t           adc_running = 0;

/* ===== DMA完成回调 ===== */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        frame_ready = 1;
    }
}

/* DMA1 Channel1 �?�?处理 */
void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}

/* ===== 配置ADC1通道 ===== */
static void ADC1_ConfigChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

static void ADC1_SelectSource(AudioSource_t src)
{
    if (src == AUDIO_SRC_MIC) {
        ADC1_ConfigChannel(ADC_CHANNEL_8);   /* PB0 */
    } else if (src == AUDIO_SRC_LINE) {
        ADC1_ConfigChannel(ADC_CHANNEL_7);   /* PA7 */
    }
}

static void ADC2_ConfigChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc2, &sConfig);
}

static uint16_t ADC2_ReadSingle(void)
{
    HAL_ADC_Start(&hadc2);
    HAL_ADC_PollForConversion(&hadc2, 10);
    {
        uint16_t val = HAL_ADC_GetValue(&hadc2);
        HAL_ADC_Stop(&hadc2);
        return val;
    }
}

/* ===== TIM3初�?�化: 8kHz ===== */
static void TIM3_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 7999;   /* 64MHz / 8000 = 8kHz */
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim3);

    TIM_MasterConfigTypeDef sMaster = {0};
    sMaster.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMaster.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMaster);
}

/* ===== DMA初�?�化 ===== */
static void DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_adc1);

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/* ===== ADC1初�?�化 ===== */
static void ADC1_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* ADC时钟: APB2(64MHz) / 6 = 10.67MHz */
    RCC_PeriphCLKInitTypeDef adc_clk = {0};
    adc_clk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    adc_clk.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    HAL_RCCEx_PeriphCLKConfig(&adc_clk);

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc1);

    /* GPIO: PA7模拟输入, PB0模拟输入 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;

    gpio.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* 默�?�配�?麦克风通道 */
    ADC1_ConfigChannel(ADC_CHANNEL_8);

    /* ADC校准 */
    HAL_ADCEx_Calibration_Start(&hadc1);
}

/* ===== ADC2初�?�化 (电位�?) ===== */
static void ADC2_Init(void)
{
    __HAL_RCC_ADC2_CLK_ENABLE();

    hadc2.Instance = ADC2;
    hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc2.Init.ContinuousConvMode = DISABLE;
    hadc2.Init.DiscontinuousConvMode = DISABLE;
    hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc2.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc2);

    /* GPIO: PB1模拟输入 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* 配置CH9 */
    ADC2_ConfigChannel(ADC_CHANNEL_9);

    HAL_ADCEx_Calibration_Start(&hadc2);
}

/* ===== �?共接�? ===== */
void BSP_ADC_Init(void)
{
    DMA_Init();
    ADC1_Init();
    ADC2_Init();
    TIM3_Init();
    current_src = AUDIO_SRC_MIC;
    adc_running = 0;
}

void BSP_ADC_SetSource(AudioSource_t src)
{
    if (src >= AUDIO_SRC_COUNT || src == AUDIO_SRC_TEST) return;

    if (current_src == src) {
        return;
    }

    {
        uint8_t was_running = adc_running;

        if (was_running) {
            HAL_ADC_Stop_DMA(&hadc1);
            HAL_TIM_Base_Stop(&htim3);
            adc_running = 0;
        }

        ADC1_SelectSource(src);
        current_src = src;
        frame_ready = 0;

        if (was_running) {
            HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_raw, ADC_BUF_SIZE);
            HAL_TIM_Base_Start(&htim3);
            adc_running = 1;
        }
    }
}

AudioSource_t BSP_ADC_GetSource(void)
{
    return current_src;
}

void BSP_ADC_Start(void)
{
    if (current_src == AUDIO_SRC_TEST || adc_running) return;

    frame_ready = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_raw, ADC_BUF_SIZE);
    HAL_TIM_Base_Start(&htim3);
    adc_running = 1;
}

void BSP_ADC_Stop(void)
{
    if (!adc_running) {
        return;
    }

    HAL_ADC_Stop_DMA(&hadc1);
    HAL_TIM_Base_Stop(&htim3);
    adc_running = 0;
    frame_ready = 0;
}

uint8_t BSP_ADC_FrameReady(void)
{
    return frame_ready;
}

void BSP_ADC_GetFrame(uint8_t *buf)
{
    /* 12位ADC�? (0-4095) �? 8�? (0-255) */
    for (uint16_t i = 0; i < ADC_BUF_SIZE; i++) {
        buf[i] = (uint8_t)(adc_raw[i] >> 4);
    }
    frame_ready = 0;
}

void BSP_ADC_GetRawStats(uint16_t *avg, uint16_t *min, uint16_t *max)
{
    uint32_t sum = 0;
    uint16_t local_min = 0xFFFFU;
    uint16_t local_max = 0U;

    for (uint16_t i = 0; i < ADC_BUF_SIZE; i++) {
        uint16_t sample = adc_raw[i];
        sum += sample;
        if (sample < local_min) {
            local_min = sample;
        }
        if (sample > local_max) {
            local_max = sample;
        }
    }

    if (avg != NULL) {
        *avg = (uint16_t)(sum / ADC_BUF_SIZE);
    }
    if (min != NULL) {
        *min = (local_min == 0xFFFFU) ? 0U : local_min;
    }
    if (max != NULL) {
        *max = local_max;
    }
}

uint16_t BSP_ADC_ReadMicProbe(void)
{
    uint32_t sum = 0;

    ADC2_ConfigChannel(ADC_CHANNEL_8);

    /* 丢弃切换通道后的首�?采样，避免受上一通道残留电压影响 */
    (void)ADC2_ReadSingle();
    for (uint8_t i = 0; i < 8U; i++) {
        sum += ADC2_ReadSingle();
    }

    ADC2_ConfigChannel(ADC_CHANNEL_9);

    return (uint16_t)(sum / 8U);
}

uint16_t BSP_ADC_ReadPot(void)
{
    ADC2_ConfigChannel(ADC_CHANNEL_9);
    (void)ADC2_ReadSingle();
    return ADC2_ReadSingle();
}
