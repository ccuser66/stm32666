#ifndef BSP_MIC_ADC_H
#define BSP_MIC_ADC_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* ADC采样参数 */
#define MIC_SAMPLE_RATE   8000    /* 采样率 8kHz */
#define MIC_BUF_SIZE      480     /* DMA缓冲 (半满=240点=一帧显示) */

/* 初始化麦克风ADC (TIM3触发 + DMA) */
void MIC_Init(void);

/* 获取一帧可显示的数据 (240点, 0-255)
 * 返回: 1=有新数据, 0=无 */
uint8_t MIC_GetFrame(uint8_t *dest, uint16_t max_len);

/* DMA半满/满中断回调 (在中断文件中调用) */
void MIC_DMA_HalfCpltCallback(void);
void MIC_DMA_CpltCallback(void);

#endif
