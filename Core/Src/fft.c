#include "fft.h"

/* 变长基2 FFT (64/128/256)，定点16位运算，适合Cortex-M3 */

/* sin查找表: sin(2*pi*k/256)*32767, k=0..64 (第一象限) */
static const int16_t sin_tbl[65] = {
        0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
     6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767
};

static int16_t sin_fix(uint16_t idx)
{
    idx &= 0xFF;
    if (idx <= 64)       return  sin_tbl[idx];
    else if (idx <= 128) return  sin_tbl[128 - idx];
    else if (idx <= 192) return -sin_tbl[idx - 128];
    else                 return -sin_tbl[256 - idx];
}

static int16_t cos_fix(uint16_t idx)
{
    return sin_fix(idx + 64);
}

/* ===== 窗函数计算 =====
 * 利用cos_fix实时计算，支持任意N，不需要存多个表
 * 返回 Q15 格式 (0 ~ 32767)
 */
static int16_t calc_window(WindowType_t win, uint16_t n, uint16_t N)
{
    if (win == WIN_RECT) return 32767;

    /* cos(2*pi*n/N) 用256点表映射: idx = n*256/N */
    int16_t c1 = cos_fix((uint16_t)((uint32_t)n * 256 / N));
    int32_t w;

    switch (win) {
    case WIN_HAMMING:
        /* 0.54 - 0.46*cos(...) = 17695 - 15073*c1/32767 */
        w = 17695 - ((int32_t)15073 * c1 >> 15);
        break;
    case WIN_HANNING:
        /* 0.5 - 0.5*cos(...) = 16384 - 16384*c1/32767 */
        w = 16384 - ((int32_t)16384 * c1 >> 15);
        break;
    case WIN_BLACKMAN: {
        /* 0.42 - 0.5*cos(...) + 0.08*cos(2*...) */
        int16_t c2 = cos_fix((uint16_t)((uint32_t)n * 512 / N));
        w = 13763 - ((int32_t)16384 * c1 >> 15) + ((int32_t)2621 * c2 >> 15);
        break;
    }
    default:
        w = 32767;
        break;
    }

    if (w < 0) w = 0;
    if (w > 32767) w = 32767;
    return (int16_t)w;
}

/* 整数平方根 */
static uint32_t isqrt(uint32_t x)
{
    if (x == 0) return 0;
    uint32_t r = x, q;
    while (1) {
        q = x / r;
        if (r <= q) return r;
        r = (r + q) >> 1;
    }
}

/* 位逆序排列 (变长) */
static void bit_reverse(int16_t *re, int16_t *im, uint16_t N)
{
    uint16_t j = 0;
    for (uint16_t i = 0; i < N; i++) {
        if (i < j) {
            int16_t tr = re[i]; re[i] = re[j]; re[j] = tr;
            int16_t ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
        uint16_t m = N >> 1;
        while (m >= 1 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
}

void FFT_Compute(const uint8_t *in, uint16_t *mag,
                 uint16_t fft_size, WindowType_t win)
{
    static int16_t re[FFT_MAX_N], im[FFT_MAX_N];
    uint16_t N = fft_size;
    uint16_t i;

    /* 限制FFT大小 */
    if (N > FFT_MAX_N) N = FFT_MAX_N;
    if (N < 16) N = 16;
    /* 确保是2的幂 */
    if (N & (N - 1)) N = 256;

    /* 计算实际直流均值 (适配MIC≈97和LINE≈128) */
    uint32_t dc_sum = 0;
    for (i = 0; i < N; i++) dc_sum += in[i];
    int16_t dc = (int16_t)(dc_sum / N);

    /* 输入预处理：去直流 + 加窗 */
    for (i = 0; i < N; i++) {
        int16_t x = (int16_t)in[i] - dc;
        if (win != WIN_RECT) {
            int16_t w = calc_window(win, i, N);
            x = (int16_t)(((int32_t)x * w) >> 15);
        }
        re[i] = x;
        im[i] = 0;
    }

    bit_reverse(re, im, N);

    /* 蝶形运算 - 旋转因子始终基于256点表 */
    for (uint16_t step = 1; step < N; step <<= 1) {
        uint16_t half = step;
        /* 映射到256点表: tbl_step = 256 / (step*2) */
        uint16_t tbl_step = 256 / (step << 1);

        for (uint16_t group = 0; group < N; group += (step << 1)) {
            for (uint16_t k = 0; k < half; k++) {
                uint16_t idx = group + k;
                uint16_t idx2 = idx + half;
                uint16_t w_idx = k * tbl_step;

                int16_t wr = cos_fix(w_idx);
                int16_t wi = -sin_fix(w_idx);

                int32_t tr = ((int32_t)re[idx2] * wr - (int32_t)im[idx2] * wi) >> 15;
                int32_t ti = ((int32_t)re[idx2] * wi + (int32_t)im[idx2] * wr) >> 15;

                re[idx2] = re[idx] - (int16_t)tr;
                im[idx2] = im[idx] - (int16_t)ti;
                re[idx]  = re[idx] + (int16_t)tr;
                im[idx]  = im[idx] + (int16_t)ti;
            }
        }
    }

    /* 计算幅度谱 (前N/2个频率分量) */
    for (i = 0; i < N / 2; i++) {
        uint32_t r2 = (int32_t)re[i] * re[i] + (int32_t)im[i] * im[i];
        mag[i] = (uint16_t)isqrt(r2);
    }
}

uint16_t FFT_CalcRMS(const uint8_t *in, uint16_t len)
{
    uint32_t sum_sq = 0;
    for (uint16_t i = 0; i < len; i++) {
        int16_t s = (int16_t)in[i] - 128;
        sum_sq += (uint32_t)(s * s);
    }
    uint32_t rms_sq = sum_sq / (len ? len : 1);
    return (uint16_t)isqrt(rms_sq);
}
