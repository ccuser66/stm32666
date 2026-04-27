#include "fft_processor.h"
#include "arm_math.h"
#include <math.h>

static arm_rfft_fast_instance_f32 fft_instance;
static float fft_input[FFT_SIZE];
static float fft_output[FFT_SIZE];

#define PI 3.14159265359f

/* 初始化FFT处理器 */
void FFT_Init(void)
{
    /* 初始化RFFT (实数FFT，更高效) */
    arm_rfft_fast_init_f32(&fft_instance, FFT_SIZE);
}

/* 执行FFT处理 */
void FFT_Process(int16_t* input, FFTResult* result)
{
    /* Step 1: int16转换为float，并进行汉明窗处理 */
    for(int i = 0; i < FFT_SIZE; i++){
        /* 汉明窗: w[n] = 0.54 - 0.46*cos(2π*n/(N-1)) */
        float window = 0.54f - 0.46f * cosf(2.0f * PI * i / (FFT_SIZE - 1));
        fft_input[i] = (float)input[i] * window / 32768.0f;
    }
    
    /* Step 2: 执行RFFT */
    arm_rfft_fast_f32(&fft_instance, fft_input, fft_output, 0);
    
    /* Step 3: 计算幅度谱 |FFT| */
    arm_cmplx_mag_f32(fft_output, result->magnitude, FFT_SIZE/2);
    
    /* Step 4: 转换为dB并计算频率 */
    for(int i = 0; i < FFT_SIZE/2; i++){
        result->magnitude[i] = 20.0f * log10f(result->magnitude[i] + 1e-10f);
        result->frequency[i] = (float)i * (AUDIO_SAMPLE_RATE / (float)FFT_SIZE);
    }
}
