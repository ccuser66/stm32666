#include "bsp_mic_adc.h"
#include <string.h>

/* =========== 硬件句柄 =========== */
static ADC_HandleTypeDef  hadc1;
DMA_HandleTypeDef  hdma_adc1;  /* 非static, 中断文件需要访问 */
static TIM_HandleTypeDef  htim3;

/* =========== DMA双缓冲 =========== */
static uint16_t adc_dma_buf[MIC_BUF_SIZE];  /* DMA写入的12bit原始数据 */

/* 帧就绪标志和帧数据 */
static volatile uint8_t frame_ready = 0;
static uint8_t frame_buf[MIC_BUF_SIZE / 2]; /* 240点 8bit */

/* =========== 内部: 将半个DMA缓冲转为8bit帧 =========== */
static void convert_half(const uint16_t *src)
{
    uint16_t len = MIC_BUF_SIZE / 2;  /* 240 */
    for (uint16_t i = 0; i < len; i++) {
        /* 12bit ADC (0-4095) -> 8bit (0-255) */
        frame_buf[i] = (uint8_t)(src[i] >> 4);
    }
    frame_ready = 1;
}

/* =========== DMA回调 =========== */
void MIC_DMA_HalfCpltCallback(void)
{
    /* 前半满: adc_dma_buf[0..239] */
    convert_half(&adc_dma_buf[0]);
}

void MIC_DMA_CpltCallback(void)
{
    /* 后半满: adc_dma_buf[240..479] */
    convert_half(&adc_dma_buf[MIC_BUF_SIZE / 2]);
}

/* =========== 外部接口 =========== */
uint8_t MIC_GetFrame(uint8_t *dest, uint16_t max_len)
{
    if (!frame_ready) return 0;

    uint16_t len = MIC_BUF_SIZE / 2;
    if (len > max_len) len = max_len;
    memcpy(dest, frame_buf, len);
    frame_ready = 0;
    return 1;
}

/* =========== TIM3初始化: 触发ADC采样 =========== */
static void MIC_TIM3_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    /* APB1=32MHz, TIM3时钟=64MHz (APB1预分频!=1时TIM时钟x2)
     * 目标: 8000Hz 触发
     * 64MHz / 8000 = 8000
     * Prescaler=7, Period=999 -> 64MHz/(8)/(1000)=8000Hz */
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 7;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 999;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim3);

    /* 配置TRGO输出为Update事件, 用于触发ADC */
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig);
}

/* =========== ADC1初始化: PA0, TIM3触发, DMA循环 =========== */
static void MIC_ADC1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* ADC时钟分频: PCLK2(64MHz) / 6 = ~10.67MHz (最大14MHz) */
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    /* PA0 模拟输入 */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* DMA1 Channel1 (ADC1专用) */
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

    /* DMA中断 */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    /* ADC配置 */
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;        /* 单通道 */
    hadc1.Init.ContinuousConvMode = DISABLE;             /* 非连续, 由TIM触发 */
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;  /* TIM3触发 */
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc1);

    /* 通道0 (PA0), 采样时间 */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;  /* 稳定采样 */
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    /* ADC校准 */
    HAL_ADCEx_Calibration_Start(&hadc1);
}

/* =========== 启动采集 =========== */
void MIC_Init(void)
{
    MIC_TIM3_Init();
    MIC_ADC1_Init();

    /* 启动ADC DMA循环采集 */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, MIC_BUF_SIZE);

    /* 启动TIM3, 开始触发ADC */
    HAL_TIM_Base_Start(&htim3);
}
