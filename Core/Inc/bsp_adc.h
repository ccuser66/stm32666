#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

/* 音�?�输入源 */
typedef enum {
    AUDIO_SRC_TEST = 0,   /* �?件测试信�? */
    AUDIO_SRC_MIC,        /* MAX9814麦克�? PB0 (ADC1_CH8) */
    AUDIO_SRC_LINE,       /* 3.5mm音�?�输�? PA7 (ADC1_CH7) */
    AUDIO_SRC_COUNT
} AudioSource_t;

/* 初�?�化ADC1+TIM3+DMA (8kHz采样) �? ADC2 (电位�?) */
void BSP_ADC_Init(void);

/* 切换音�?�输入源 */
void BSP_ADC_SetSource(AudioSource_t src);
AudioSource_t BSP_ADC_GetSource(void);

/* 开�?/停�?�采�? */
void BSP_ADC_Start(void);
void BSP_ADC_Stop(void);

/* 查�?�是否有新一帧数�?就绪 (256�?) */
uint8_t BSP_ADC_FrameReady(void);

/* 获取一帧数�? (uint8_t[256], 0-255) */
void BSP_ADC_GetFrame(uint8_t *buf);

/* 读取当前 DMA 缓冲的原�?12位ADC统�?信息 */
void BSP_ADC_GetRawStats(uint16_t *avg, uint16_t *min, uint16_t *max);

/* 使用 ADC2 对HB0(CH8) 做一次直读，绕过 ADC1+DMA 链路 */
uint16_t BSP_ADC_ReadMicProbe(void);

/* 读取电位器�? (0-4095) */
uint16_t BSP_ADC_ReadPot(void);

#endif
