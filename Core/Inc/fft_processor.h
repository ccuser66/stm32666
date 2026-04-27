#ifndef FFT_PROCESSOR_H
#define FFT_PROCESSOR_H

#include <stdint.h>
#include "config.h"

/* FFT处理结果结构体 */
typedef struct {
    float magnitude[FFT_SIZE/2];      // 幅度谱
    float frequency[FFT_SIZE/2];      // 频率轴
} FFTResult;

/* 初始化FFT处理器 */
void FFT_Init(void);

/* 执行FFT处理 */
void FFT_Process(int16_t* input, FFTResult* result);

#endif /* FFT_PROCESSOR_H */
