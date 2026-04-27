#ifndef FFT_H
#define FFT_H

#include <stdint.h>

#define FFT_MAX_N  256

/* 窗函数类型 */
typedef enum {
    WIN_RECT = 0,     /* 矩形窗 (无窗) */
    WIN_HAMMING,      /* Hamming窗 */
    WIN_HANNING,      /* Hanning窗 */
    WIN_BLACKMAN,     /* Blackman窗 */
    WIN_COUNT
} WindowType_t;

/* 对 fft_size 个采样点做原位基2 FFT
 * in:       uint8_t[] 原始ADC数据 (0-255)
 * mag:      uint16_t[] 输出幅度谱 (前 fft_size/2 个)
 * fft_size: FFT点数 (64, 128, 或 256)
 * win:      窗函数类型
 */
void FFT_Compute(const uint8_t *in, uint16_t *mag,
                 uint16_t fft_size, WindowType_t win);

/* 计算一帧数据的RMS (0-128) */
uint16_t FFT_CalcRMS(const uint8_t *in, uint16_t len);

#endif
