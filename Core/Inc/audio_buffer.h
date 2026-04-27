#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* 初始化音频缓冲 */
void AudioBuffer_Init(void);

/* 从蓝牙数据加载音频样本 */
void AudioBuffer_LoadFromBT(uint8_t* bt_data, uint16_t len);

/* 检查缓冲是否准备好处理 */
bool AudioBuffer_IsReady(void);

/* 获取音频数据指针 */
int16_t* AudioBuffer_GetData(void);

/* 重置缓冲 */
void AudioBuffer_Reset(void);

#endif /* AUDIO_BUFFER_H */
