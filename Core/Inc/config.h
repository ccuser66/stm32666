#ifndef CONFIG_H
#define CONFIG_H

/* ============ 系统配置 ============ */
#define SYSTEM_TICK_FREQ 1000  // 系统时钟频率 Hz (1ms中断一次)

/* ============ 蓝牙配置 ============ */
#define BT_UART_BAUDRATE 9600
#define BT_BUFFER_SIZE 512

/* ============ 音频配置 ============ */
#define AUDIO_SAMPLE_RATE 16000    // 16kHz 采样率
#define AUDIO_BUFFER_SIZE 2048     // FFT大小
#define AUDIO_BIT_DEPTH 16         // 16-bit

/* ============ LCD配置 ============ */
#define LCD_WIDTH 240
#define LCD_HEIGHT 320

/* ============ FFT配置 ============ */
#define FFT_SIZE 2048
#define FFT_LOG2_SIZE 11

/* ============ 按键配置 ============ */
#define DEBOUNCE_TIME_MS 20

/* ============ 显示模式 ============ */
typedef enum {
    MODE_TIME_DOMAIN = 0,    // 时域波形
    MODE_FREQUENCY = 1,      // 频域频谱
    MODE_COUNT = 2
} DisplayMode;

#endif /* CONFIG_H */
