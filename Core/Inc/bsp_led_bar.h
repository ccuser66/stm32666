#ifndef BSP_LED_BAR_H
#define BSP_LED_BAR_H

#include <stdint.h>

#define LED_BAR_COUNT  8

/* 初始化PB1-PB8为推挽输出 */
void LedBar_Init(void);

/* 用FFT幅度谱驱动8个LED
 * mag: FFT幅度数组 (至少FFT_N/2=128个元素)
 * num_bins: 幅度数组长度 (通常128)
 */
void LedBar_Update(const uint16_t *mag, uint16_t num_bins);

/* 全部关闭 */
void LedBar_AllOff(void);

#endif
