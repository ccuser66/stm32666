#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <stdint.h>
#include "config.h"
#include "fft_processor.h"

/* 初始化UI系统 */
void UI_Init(void);

/* 渲染时域波形 */
void UI_RenderTimeDomain(int16_t* data, uint16_t size);

/* 渲染频域频谱 */
void UI_RenderFrequency(FFTResult* fft_result);

/* 切换显示模式 */
void UI_SwapMode(void);

/* 获取当前模式 */
DisplayMode UI_GetCurrentMode(void);

#endif /* UI_RENDERER_H */
