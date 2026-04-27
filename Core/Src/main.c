/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 基于STM32的音频信号可视化系统
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_lcd.h"
#include "audio_display.h"
#include "bsp_adc.h"
#include "bsp_buzzer.h"
#include "bsp_uart.h"
#include "bsp_led_bar.h"
#include "fft.h"
#include <stdlib.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ===== 系统状�? ===== */
typedef enum { STATE_MENU, STATE_DISPLAY } AppState_t;

/* ===== 按键ID ===== */
typedef enum { KEY_NONE = 0, KEY_UP, KEY_DOWN, KEY_OK, KEY_MENU } KeyId_t;

/* ===== 菜单�? ===== */
#define MENU_ITEMS  6

#define PB0_OUTPUT_TEST  0U
/* 0: 输入源MIC  1: 输入源LINE
 * 2: 时域波形  3: 频谱分析  4: 瀑布�?  5: VU音量�? */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t audio_frame[FFT_MAX_N];

/* 系统状�? */
static AppState_t g_state = STATE_MENU;
static DisplayMode_t g_mode = DISPLAY_MODE_TIME;
static AudioSource_t g_audio_src = AUDIO_SRC_MIC;

/* 菜单 */
static uint8_t g_menu_cursor = 0;   /* 0~5 */
static uint8_t g_menu_prev_cursor = 0xFF;

/* 拨码开�? */
static uint32_t g_dip_last_tick = 0;
static uint16_t g_fft_size = 256;
static WindowType_t g_win_type = WIN_HAMMING;

/* 电位�?/蜂鸣�? */
static uint32_t g_pot_last_tick = 0;
static uint16_t g_alarm_threshold = 64;
static uint16_t g_last_rms = 0;
static uint8_t g_last_peak = 0;
static uint16_t g_raw_avg = 0;
static uint8_t g_raw_peak = 0;
static uint16_t g_raw_avg12 = 0;
static uint16_t g_raw_min12 = 0;
static uint16_t g_raw_max12 = 0;
static uint16_t g_mic_probe12 = 0;
static uint32_t g_adc_frame_count = 0;
static uint8_t g_remote_fft_cfg = 0;
static uint8_t g_display_zoom = 1;
static uint8_t g_enhanced_denoise_enabled = 1;
static uint8_t g_denoise_buf[FFT_MAX_N];
static int16_t g_power_noise_prev_input = 0;
static int16_t g_power_noise_prev_output = 0;

/* 杂项 */
static uint32_t g_led_last_tick = 0;
static uint32_t g_dbg_update_tick = 0;
static uint32_t g_spectrum_tx_tick = 0;
static uint32_t g_status_tx_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);

/* USER CODE BEGIN PFP */
static void DrawMenu(void);
static void DrawMenuConfigLine(void);
static void DrawMenuStatusLine(void);
static void DrawMenuDiagLine(void);
static void DrawDisplayDiagLine(void);
#if PB0_OUTPUT_TEST
static void EnablePB0HighTestMode(void);
#endif
static void ApplyFFTConfig(uint16_t fft_size, WindowType_t win_type, uint8_t remote_cfg);
static void SendDisplayFrame(const uint8_t *data, uint16_t len);
static void SendErrorFrame(const char *msg);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ===== 4�?消抖读取 ===== */
static KeyId_t ScanKey(void)
{
    static uint8_t  last[4] = {1,1,1,1};  /* 上�?�电�?: 1=�?�? */
    static uint32_t tick[4] = {0};

    const GPIO_TypeDef *port[] = { KEY_UP_Port, KEY_DOWN_Port, KEY_OK_Port, KEY_MENU_Port };
    const uint16_t      pin[]  = { KEY_UP_Pin,  KEY_DOWN_Pin,  KEY_OK_Pin,  KEY_MENU_Pin  };
    const KeyId_t       id[]   = { KEY_UP,      KEY_DOWN,      KEY_OK,      KEY_MENU      };

    uint32_t now = HAL_GetTick();
    for (int i = 0; i < 4; i++) {
        uint8_t cur = (HAL_GPIO_ReadPin((GPIO_TypeDef *)port[i], pin[i]) == GPIO_PIN_RESET) ? 0 : 1;
        if (cur == 0 && last[i] == 1 && (now - tick[i]) > 20) {
            /* 下降�? = 按下 */
            last[i] = 0;
            tick[i] = now;
            return id[i];
        }
        if (cur == 1) last[i] = 1;
    }
    return KEY_NONE;
}

/* ===== 读取拨码开�? ===== */
static void ReadDipSwitch(void)
{
    uint8_t sw1 = (HAL_GPIO_ReadPin(DIP_SW1_Port, DIP_SW1_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    uint8_t sw2 = (HAL_GPIO_ReadPin(DIP_SW2_Port, DIP_SW2_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    uint8_t win_idx = sw1 | (sw2 << 1);
    if (win_idx >= WIN_COUNT) win_idx = 0;

    uint8_t sw3 = (HAL_GPIO_ReadPin(DIP_SW3_Port, DIP_SW3_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    uint8_t sw4 = (HAL_GPIO_ReadPin(DIP_SW4_Port, DIP_SW4_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    uint8_t size_idx = sw3 | (sw4 << 1);
    static const uint16_t fft_sizes[] = {64, 128, 256, 256};

    ApplyFFTConfig(fft_sizes[size_idx], (WindowType_t)win_idx, 0U);
}

/* ===== 数字�?字�?�串工具 ===== */
static void uint_to_str(char *buf, uint16_t val, uint8_t digits)
{
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (val % 10);
        val /= 10;
    }
    buf[digits] = '\0';
}

static void uint_to_dec_str(char *buf, uint16_t val)
{
    char rev[5];
    uint8_t len = 0;

    do {
        rev[len++] = (char)('0' + (val % 10U));
        val /= 10U;
    } while (val > 0U && len < sizeof(rev));

    for (uint8_t i = 0; i < len; i++) {
        buf[i] = rev[len - 1U - i];
    }
    buf[len] = '\0';
}

static void append_text(char *buf, uint16_t buf_size, const char *text)
{
    uint16_t pos = (uint16_t)strlen(buf);

    while (pos + 1U < buf_size && *text != '\0') {
        buf[pos++] = *text++;
    }
    buf[pos] = '\0';
}

static void append_uint(char *buf, uint16_t buf_size, uint16_t val)
{
    char tmp[6];
    uint_to_dec_str(tmp, val);
    append_text(buf, buf_size, tmp);
}

static void UART_SendUInt(uint16_t val)
{
    char buf[6];
    uint_to_dec_str(buf, val);
    BSP_UART_SendString(buf);
}

static const char *SourceName(AudioSource_t src)
{
    switch (src) {
    case AUDIO_SRC_MIC:  return "MIC";
    case AUDIO_SRC_LINE: return "LINE";
    default:             return "TEST";
    }
}

static const char *ModeName(DisplayMode_t mode)
{
    switch (mode) {
    case DISPLAY_MODE_TIME:      return "TIME";
    case DISPLAY_MODE_SPECTRUM:  return "SPECTRUM";
    case DISPLAY_MODE_WATERFALL: return "WATERFALL";
    case DISPLAY_MODE_VU:        return "VU";
    default:                     return "TIME";
    }
}

static const char *WindowName(WindowType_t win)
{
    static const char * const win_names[] = {"RECT", "HAMM", "HANN", "BLKM"};
    if ((uint8_t)win >= WIN_COUNT) {
        return "RECT";
    }
    return win_names[win];
}

static void RefreshCurrentScreen(void)
{
    if (g_state == STATE_MENU) {
        DrawMenuConfigLine();
        DrawMenuStatusLine();
        DrawMenuDiagLine();
    }
}

static void ApplyFFTConfig(uint16_t fft_size, WindowType_t win_type, uint8_t remote_cfg)
{
    uint8_t config_changed;

    if ((fft_size != 64U && fft_size != 128U && fft_size != 256U) || (uint8_t)win_type >= WIN_COUNT) {
        return;
    }

    config_changed = (uint8_t)((g_fft_size != fft_size) || (g_win_type != win_type));

    g_fft_size = fft_size;
    g_win_type = win_type;
    g_remote_fft_cfg = remote_cfg;
    AudioDisplay_SetFFTConfig(g_fft_size, g_win_type);

    if (!config_changed) {
        return;
    }

    if (g_state == STATE_MENU) {
        DrawMenuConfigLine();
    } else if (g_state == STATE_DISPLAY) {
        AudioDisplay_SetMode(g_mode);
    }
}

static void ApplyAlarmThreshold(uint16_t threshold)
{
    if (threshold > 128) {
        threshold = 128;
    }
    g_alarm_threshold = threshold;
    RefreshCurrentScreen();
}

static uint8_t PotToDisplayZoom(uint16_t pot_val)
{
    return (uint8_t)(1U + ((uint32_t)pot_val * 3U / 4095U));
}

static void UpdateDisplayZoomFromPot(void)
{
    static const uint16_t zoom_thresholds[] = {1024U, 2048U, 3072U};
    const uint16_t hysteresis = 96U;
    uint16_t pot_val = BSP_ADC_ReadPot();
    uint8_t next_zoom = PotToDisplayZoom(pot_val);

    if (next_zoom == g_display_zoom) {
        return;
    }

    if (next_zoom > g_display_zoom) {
        uint8_t boundary_idx = (uint8_t)(g_display_zoom - 1U);
        if (boundary_idx < 3U && pot_val < (uint16_t)(zoom_thresholds[boundary_idx] + hysteresis)) {
            return;
        }
    } else {
        uint8_t boundary_idx = (uint8_t)(next_zoom - 1U);
        if (boundary_idx < 3U && pot_val >= (uint16_t)(zoom_thresholds[boundary_idx] - hysteresis)) {
            return;
        }
    }

    g_display_zoom = next_zoom;
    AudioDisplay_SetHorizontalZoom(g_display_zoom);
    RefreshCurrentScreen();
}

static void ApplyFFTSize(uint16_t fft_size)
{
    ApplyFFTConfig(fft_size, g_win_type, 1U);
}

static void ApplyWindowType(WindowType_t win_type)
{
    ApplyFFTConfig(g_fft_size, win_type, 1U);
}

static void ApplyAudioSource(AudioSource_t src)
{
    if (src != AUDIO_SRC_MIC && src != AUDIO_SRC_LINE) {
        return;
    }

    g_power_noise_prev_input = 0;
    g_power_noise_prev_output = 0;
    g_audio_src = src;
    BSP_ADC_SetSource(src);
    BSP_ADC_Start();
    if (g_state == STATE_MENU) {
        DrawMenu();
    }
}

static void ApplyDisplayMode(DisplayMode_t mode)
{
    if ((uint8_t)mode >= DISPLAY_MODE_COUNT) {
        return;
    }
    g_mode = mode;
    g_state = STATE_DISPLAY;
    AudioDisplay_SetMode(g_mode);
}

static void ApplyViewState(AppState_t state)
{
    g_state = state;
    if (state == STATE_MENU) {
        g_menu_prev_cursor = 0xFF;
        DrawMenu();
    } else {
        AudioDisplay_SetMode(g_mode);
    }
}

static void ReleaseHardwareControl(void)
{
    ReadDipSwitch();

    RefreshCurrentScreen();
}

static uint8_t CalcPeakLevel(const uint8_t *data, uint16_t len)
{
    uint8_t peak = 0;

    if (data == NULL) {
        return 0;
    }

    for (uint16_t i = 0; i < len; i++) {
        uint8_t sample = data[i];
        uint8_t level = (sample >= 128U) ? (uint8_t)(sample - 128U) : (uint8_t)(128U - sample);
        if (level > peak) {
            peak = level;
        }
    }

    return peak;
}

static void UpdateRawFrameStats(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;

    if (data == NULL || len == 0U) {
        return;
    }

    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }

    g_raw_avg = (uint16_t)(sum / len);
    g_raw_peak = CalcPeakLevel(data, len);
    BSP_ADC_GetRawStats(&g_raw_avg12, &g_raw_min12, &g_raw_max12);
    g_adc_frame_count++;
}

static uint8_t Median3(uint8_t a, uint8_t b, uint8_t c)
{
    if (a > b) {
        uint8_t tmp = a;
        a = b;
        b = tmp;
    }
    if (b > c) {
        uint8_t tmp = b;
        b = c;
        c = tmp;
    }
    if (a > b) {
        b = a;
    }
    return b;
}

static void ApplyPowerNoiseFilter(uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U) {
        return;
    }

    for (uint16_t i = 0; i < len; i++) {
        int16_t input = (int16_t)data[i] - 128;
        int32_t filtered = ((int32_t)31 * ((int32_t)g_power_noise_prev_output + input - g_power_noise_prev_input)) / 32;
        int16_t output;

        if (filtered < -128) filtered = -128;
        if (filtered > 127) filtered = 127;

        g_power_noise_prev_input = input;
        g_power_noise_prev_output = (int16_t)filtered;

        output = (int16_t)(filtered + 128);
        if (output < 0) output = 0;
        if (output > 255) output = 255;
        data[i] = (uint8_t)output;
    }
}

static void ApplyMicAutoGain(uint8_t *data, uint16_t len)
{
    uint8_t peak = 0;
    uint8_t gain = 1;

    if (g_audio_src != AUDIO_SRC_MIC || data == NULL || len == 0U) {
        return;
    }

    for (uint16_t i = 0; i < len; i++) {
        uint8_t sample = data[i];
        uint8_t level = (sample >= 128U) ? (uint8_t)(sample - 128U) : (uint8_t)(128U - sample);
        if (level > peak) {
            peak = level;
        }
    }

    if (peak < 4U) {
        gain = 8U;
    } else if (peak < 8U) {
        gain = 6U;
    } else if (peak < 16U) {
        gain = 4U;
    } else if (peak < 24U) {
        gain = 3U;
    } else if (peak < 40U) {
        gain = 2U;
    }

    if (gain == 1U) {
        return;
    }

    for (uint16_t i = 0; i < len; i++) {
        int16_t centered = (int16_t)data[i] - 128;
        int16_t boosted = (int16_t)(centered * (int16_t)gain);

        if (boosted < -128) boosted = -128;
        if (boosted > 127) boosted = 127;

        data[i] = (uint8_t)(boosted + 128);
    }
}

static void ApplyEnhancedDenoise(uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 3U) {
        return;
    }

    g_denoise_buf[0] = data[0];
    for (uint16_t i = 1; i < (uint16_t)(len - 1U); i++) {
        g_denoise_buf[i] = Median3(data[i - 1U], data[i], data[i + 1U]);
    }
    g_denoise_buf[len - 1U] = data[len - 1U];

    {
        int16_t filtered = (int16_t)g_denoise_buf[0] - 128;
        data[0] = g_denoise_buf[0];
        for (uint16_t i = 1; i < len; i++) {
            int16_t sample = (int16_t)g_denoise_buf[i] - 128;
            int16_t delta = (int16_t)(sample - filtered);
            int16_t step = (int16_t)(delta / ((delta > 24 || delta < -24) ? 2 : 4));

            if (step == 0 && delta != 0) {
                step = (delta > 0) ? 1 : -1;
            }
            filtered = (int16_t)(filtered + step);

            int16_t output = (int16_t)(filtered + 128);
            if (output < 0) output = 0;
            if (output > 255) output = 255;
            data[i] = (uint8_t)output;
        }
    }
}

static void SendWaveFrame(const uint8_t *data, uint16_t len)
{
    enum { APP_WAVE_POINTS = 64 };
    uint16_t count = (len < APP_WAVE_POINTS) ? len : APP_WAVE_POINTS;

    if (data == NULL || count == 0U) {
        return;
    }

    BSP_UART_SendString("WAV:");
    for (uint16_t i = 0; i < count; i++) {
        uint16_t index = (uint16_t)(((uint32_t)i * len) / count);
        if (index >= len) {
            index = (uint16_t)(len - 1U);
        }
        UART_SendUInt(data[index]);
        BSP_UART_SendString((i + 1U < count) ? "," : "\r\n");
    }
}

static void SendVuFrame(uint16_t rms, uint8_t peak)
{
    BSP_UART_SendString("VU:");
    UART_SendUInt(rms);
    BSP_UART_SendString(",");
    UART_SendUInt(peak);
    BSP_UART_SendString("\r\n");
}

static void SendDisplayFrame(const uint8_t *data, uint16_t len)
{
    switch (g_mode) {
    case DISPLAY_MODE_TIME:
        SendWaveFrame(data, len);
        break;
    case DISPLAY_MODE_SPECTRUM:
    case DISPLAY_MODE_WATERFALL:
    {
        uint16_t num_bins = 0;
        const uint16_t *mag = AudioDisplay_GetFFTMag(&num_bins);
        BSP_UART_SendSpectrum(mag, num_bins);
        break;
    }
    case DISPLAY_MODE_VU:
        SendVuFrame(g_last_rms, g_last_peak);
        break;
    default:
        break;
    }
}

static void SendStatusFrame(void)
{
    BSP_UART_SendString("STAT:SRC=");
    BSP_UART_SendString(SourceName(g_audio_src));
    BSP_UART_SendString(",MODE=");
    BSP_UART_SendString(ModeName(g_mode));
    BSP_UART_SendString(",FFT=");
    UART_SendUInt(g_fft_size);
    BSP_UART_SendString(",WIN=");
    BSP_UART_SendString(WindowName(g_win_type));
    BSP_UART_SendString(",WIDTH=");
    UART_SendUInt(g_display_zoom);
    BSP_UART_SendString(",DENOISE=");
    BSP_UART_SendString(g_enhanced_denoise_enabled ? "ON" : "OFF");
    BSP_UART_SendString(",TH=");
    UART_SendUInt(g_alarm_threshold);
    BSP_UART_SendString(",RMS=");
    UART_SendUInt(g_last_rms);
    BSP_UART_SendString(",ALARM=");
    UART_SendUInt((g_last_rms > g_alarm_threshold) ? 1U : 0U);
    BSP_UART_SendString(",CFG=");
    BSP_UART_SendString(g_remote_fft_cfg ? "APP" : "DIP");
    BSP_UART_SendString(",THSRC=APP\r\n");
}

static void SendAckFrame(const char *msg)
{
    BSP_UART_SendString("ACK:");
    BSP_UART_SendString(msg);
    BSP_UART_SendString("\r\n");
}

static void SendErrorFrame(const char *msg)
{
    BSP_UART_SendString("ERR:");
    BSP_UART_SendString(msg);
    BSP_UART_SendString("\r\n");
}

static void ProcessBluetoothCommands(void)
{
    char line[128];

    while (BSP_UART_ReadLine(line, sizeof(line))) {
        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "GET") == 0) {
            SendStatusFrame();
            if (g_state == STATE_DISPLAY) {
                SendDisplayFrame(audio_frame, 256U);
            }
            continue;
        }

        if (strcmp(line, "CTRL:LOCAL") == 0) {
            ReleaseHardwareControl();
            SendAckFrame("CTRL:LOCAL");
            SendStatusFrame();
            if (g_state == STATE_DISPLAY) {
                SendDisplayFrame(audio_frame, 256U);
            }
            continue;
        }

        if (strcmp(line, "PING") == 0) {
            SendAckFrame("PONG");
            continue;
        }

        if (strcmp(line, "DENOISE:ON") == 0) {
            g_enhanced_denoise_enabled = 1U;
            SendAckFrame("DENOISE:ON");
            SendStatusFrame();
            continue;
        }

        if (strcmp(line, "DENOISE:OFF") == 0) {
            g_enhanced_denoise_enabled = 0U;
            SendAckFrame("DENOISE:OFF");
            SendStatusFrame();
            continue;
        }

        if (strncmp(line, "SRC:", 4) == 0) {
            if (strcmp(line + 4, "MIC") == 0) {
                ApplyAudioSource(AUDIO_SRC_MIC);
                SendAckFrame("SRC:MIC");
                SendStatusFrame();
                if (g_state == STATE_DISPLAY) {
                    SendDisplayFrame(audio_frame, 256U);
                }
            } else if (strcmp(line + 4, "LINE") == 0) {
                ApplyAudioSource(AUDIO_SRC_LINE);
                SendAckFrame("SRC:LINE");
                SendStatusFrame();
                if (g_state == STATE_DISPLAY) {
                    SendDisplayFrame(audio_frame, 256U);
                }
            } else {
                SendErrorFrame("SRC");
            }
            continue;
        }

        if (strncmp(line, "MODE:", 5) == 0) {
            if (strcmp(line + 5, "TIME") == 0) {
                ApplyDisplayMode(DISPLAY_MODE_TIME);
            } else if (strcmp(line + 5, "SPECTRUM") == 0) {
                ApplyDisplayMode(DISPLAY_MODE_SPECTRUM);
            } else if (strcmp(line + 5, "WATERFALL") == 0) {
                ApplyDisplayMode(DISPLAY_MODE_WATERFALL);
            } else if (strcmp(line + 5, "VU") == 0) {
                ApplyDisplayMode(DISPLAY_MODE_VU);
            } else {
                SendErrorFrame("MODE");
                continue;
            }
            SendAckFrame(line);
            SendStatusFrame();
            SendDisplayFrame(audio_frame, 256U);
            continue;
        }

        if (strncmp(line, "VIEW:", 5) == 0) {
            if (strcmp(line + 5, "MENU") == 0) {
                ApplyViewState(STATE_MENU);
            } else if (strcmp(line + 5, "DISPLAY") == 0) {
                ApplyViewState(STATE_DISPLAY);
            } else {
                SendErrorFrame("VIEW");
                continue;
            }
            SendAckFrame(line);
            SendStatusFrame();
            if (g_state == STATE_DISPLAY) {
                SendDisplayFrame(audio_frame, 256U);
            }
            continue;
        }

        if (strncmp(line, "FFT:", 4) == 0) {
            uint16_t fft_size = (uint16_t)strtoul(line + 4, NULL, 10);
            if (fft_size == 64 || fft_size == 128 || fft_size == 256) {
                ApplyFFTSize(fft_size);
                SendAckFrame(line);
                SendStatusFrame();
                if (g_state == STATE_DISPLAY) {
                    SendDisplayFrame(audio_frame, 256U);
                }
            } else {
                SendErrorFrame("FFT");
            }
            continue;
        }

        if (strncmp(line, "WIN:", 4) == 0) {
            if (strcmp(line + 4, "RECT") == 0) {
                ApplyWindowType(WIN_RECT);
            } else if (strcmp(line + 4, "HAMM") == 0) {
                ApplyWindowType(WIN_HAMMING);
            } else if (strcmp(line + 4, "HANN") == 0) {
                ApplyWindowType(WIN_HANNING);
            } else if (strcmp(line + 4, "BLKM") == 0) {
                ApplyWindowType(WIN_BLACKMAN);
            } else {
                SendErrorFrame("WIN");
                continue;
            }
            SendAckFrame(line);
            SendStatusFrame();
            if (g_state == STATE_DISPLAY) {
                SendDisplayFrame(audio_frame, 256U);
            }
            continue;
        }

        if (strncmp(line, "TH:", 3) == 0) {
            uint16_t threshold = (uint16_t)strtoul(line + 3, NULL, 10);
            if (threshold <= 128U) {
                ApplyAlarmThreshold(threshold);
                SendAckFrame(line);
                SendStatusFrame();
            } else {
                SendErrorFrame("TH");
            }
            continue;
        }

        SendErrorFrame("CMD");
    }
}

static void DrawMenuConfigLine(void)
{
    static const char * const win_names[] = {"RECT", "HAMM", "HANN", "BLKM"};
    char cfg[24];

    strcpy(cfg, "FFT:");
    uint_to_str(cfg + 4, g_fft_size, 3);
    cfg[7] = ' ';
    strncpy(cfg + 8, win_names[g_win_type], 4);
    cfg[12] = ' ';
    cfg[13] = 'W';
    cfg[14] = ':';
    cfg[15] = (char)('0' + g_display_zoom);
    cfg[16] = 'x';
    cfg[17] = '\0';

    LCD_FillRect(10, 226, 200, 10, BLACK);
    LCD_DrawString(10, 226, cfg, CYAN, BLACK);
}

static void DrawMenuStatusLine(void)
{
    char info[32];

    strcpy(info, "RMS:");
    uint_to_str(info + 4, g_last_rms, 3);
    info[7] = ' ';
    strcpy(info + 8, "TH:");
    uint_to_str(info + 11, g_alarm_threshold, 3);
    info[14] = ' ';
    info[15] = 'W';
    info[16] = ':';
    info[17] = (char)('0' + g_display_zoom);
    info[18] = 'x';
    info[19] = '\0';

    LCD_FillRect(10, 300, 180, 10, BLACK);
    LCD_DrawString(10, 300, info,
        (g_last_rms > g_alarm_threshold) ? RED : GREEN, BLACK);
}

static void DrawMenuDiagLine(void)
{
    char diag[32];
    diag[0] = '\0';

    append_text(diag, sizeof(diag), "12b A");
    append_uint(diag, sizeof(diag), g_raw_avg12);
    append_text(diag, sizeof(diag), " X");
    append_uint(diag, sizeof(diag), g_raw_max12);
    append_text(diag, sizeof(diag), " M");
    append_uint(diag, sizeof(diag), g_mic_probe12);
    append_text(diag, sizeof(diag), " F");
    append_uint(diag, sizeof(diag), (uint16_t)(g_adc_frame_count % 1000U));

    LCD_FillRect(10, 310, 220, 8, BLACK);
    LCD_DrawString(10, 310, diag, YELLOW, BLACK);
}

static void DrawDisplayDiagLine(void)
{
    char diag[32];
    diag[0] = '\0';

    append_text(diag, sizeof(diag), "12b A");
    append_uint(diag, sizeof(diag), g_raw_avg12);
    append_text(diag, sizeof(diag), " X");
    append_uint(diag, sizeof(diag), g_raw_max12);
    append_text(diag, sizeof(diag), " M");
    append_uint(diag, sizeof(diag), g_mic_probe12);
    append_text(diag, sizeof(diag), " F");
    append_uint(diag, sizeof(diag), (uint16_t)(g_adc_frame_count % 1000U));

    LCD_FillRect(0, STATUS_Y + 18, 240, 8, BLACK);
    LCD_DrawString(4, STATUS_Y + 18, diag, YELLOW, BLACK);
}

/* ===== 绘制菜单界面 ===== */
static void DrawMenu(void)
{
    LCD_Clear(BLACK);

    /* 标�?? */
    LCD_DrawString(20, 10, "Audio Visualizer", WHITE, BLACK);
    LCD_DrawHLine(0, 28, 240, GRAY);

    /* 音�?�输入源 */
    LCD_DrawString(10, 40, "[Audio Source]", GRAY, BLACK);

    uint16_t c0 = (g_menu_cursor == 0) ? BLACK : WHITE;
    uint16_t b0 = (g_menu_cursor == 0) ? CYAN  : BLACK;
    uint16_t c1 = (g_menu_cursor == 1) ? BLACK : WHITE;
    uint16_t b1 = (g_menu_cursor == 1) ? CYAN  : BLACK;

    /* 当前选中的源加标�? */
    char mic_label[20], line_label[20];
    strcpy(mic_label,  "  MIC  ");
    strcpy(line_label, "  LINE ");
    if (g_audio_src == AUDIO_SRC_MIC)  mic_label[0]  = '*';
    if (g_audio_src == AUDIO_SRC_LINE) line_label[0] = '*';

    LCD_FillRect(20, 56, 200, 14, b0);
    LCD_DrawString(24, 58, mic_label, c0, b0);
    LCD_FillRect(20, 76, 200, 14, b1);
    LCD_DrawString(24, 78, line_label, c1, b1);

    /* 显示模式 */
    LCD_DrawHLine(0, 100, 240, GRAY);
    LCD_DrawString(10, 108, "[Display Mode]", GRAY, BLACK);

    static const char * const mode_names[] = {
        "  Waveform  ",
        "  Spectrum  ",
        "  Waterfall ",
        "  VU Meter  "
    };
    for (int i = 0; i < 4; i++) {
        uint8_t idx = i + 2;
        uint16_t fg = (g_menu_cursor == idx) ? BLACK : WHITE;
        uint16_t bg = (g_menu_cursor == idx) ? CYAN  : BLACK;
        uint16_t y = 124 + i * 22;
        LCD_FillRect(20, y, 200, 14, bg);
        LCD_DrawString(24, y + 2, mode_names[i], fg, bg);
    }

    /* 拨码开关状�? */
    LCD_DrawHLine(0, 218, 240, GRAY);
    DrawMenuConfigLine();

    /* 操作提示 */
    LCD_DrawHLine(0, 248, 240, GRAY);
    LCD_DrawString(10, 258, "UP/DOWN:Select", GRAY, BLACK);
    LCD_DrawString(10, 274, "OK:Confirm  MENU:Back", GRAY, BLACK);

    /* RMS / 阈值信�? */
    DrawMenuStatusLine();
    DrawMenuDiagLine();
}

/* ===== �?更新菜单光标 (避免全屏重绘) ===== */
static void UpdateMenuCursor(void)
{
    if (g_menu_cursor == g_menu_prev_cursor) return;

    static const char * const src_names[]  = {"  MIC  ", "  LINE "};
    static const char * const mode_names[] = {
        "  Waveform  ", "  Spectrum  ", "  Waterfall ", "  VU Meter  "
    };

    /* 取消旧光�? */
    if (g_menu_prev_cursor < MENU_ITEMS) {
        uint16_t y_old;
        const char *txt_old;
        if (g_menu_prev_cursor < 2) {
            y_old = 56 + g_menu_prev_cursor * 20;
            txt_old = src_names[g_menu_prev_cursor];
        } else {
            y_old = 124 + (g_menu_prev_cursor - 2) * 22;
            txt_old = mode_names[g_menu_prev_cursor - 2];
        }
        /* 加上当前源标�? */
        char label[20];
        strcpy(label, txt_old);
        if (g_menu_prev_cursor == 0 && g_audio_src == AUDIO_SRC_MIC)   label[0] = '*';
        if (g_menu_prev_cursor == 1 && g_audio_src == AUDIO_SRC_LINE)  label[0] = '*';

        LCD_FillRect(20, y_old, 200, 14, BLACK);
        LCD_DrawString(24, y_old + 2, label, WHITE, BLACK);
    }

    /* 绘制新光�? */
    {
        uint16_t y_new;
        const char *txt_new;
        if (g_menu_cursor < 2) {
            y_new = 56 + g_menu_cursor * 20;
            txt_new = src_names[g_menu_cursor];
        } else {
            y_new = 124 + (g_menu_cursor - 2) * 22;
            txt_new = mode_names[g_menu_cursor - 2];
        }
        char label[20];
        strcpy(label, txt_new);
        if (g_menu_cursor == 0 && g_audio_src == AUDIO_SRC_MIC)   label[0] = '*';
        if (g_menu_cursor == 1 && g_audio_src == AUDIO_SRC_LINE)  label[0] = '*';

        LCD_FillRect(20, y_new, 200, 14, CYAN);
        LCD_DrawString(24, y_new + 2, label, BLACK, CYAN);
    }

    g_menu_prev_cursor = g_menu_cursor;
}

/* ===== 菜单�?认动�? ===== */
static void MenuConfirm(void)
{
    if (g_menu_cursor == 0) {
        ApplyAudioSource(AUDIO_SRC_MIC);
    } else if (g_menu_cursor == 1) {
        ApplyAudioSource(AUDIO_SRC_LINE);
    } else {
        /* 选择显示模式并进�? */
        g_mode = (DisplayMode_t)(g_menu_cursor - 2);
        g_state = STATE_DISPLAY;
        AudioDisplay_SetMode(g_mode);
    }
}

#if PB0_OUTPUT_TEST
static void EnablePB0HighTestMode(void)
{
    GPIO_InitTypeDef gpio = {0};

    BSP_ADC_Stop();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    LCD_Clear(BLACK);
    LCD_DrawString(24, 40, "PB0 HIGH TEST", YELLOW, BLACK);
    LCD_DrawString(8, 80, "DISCONNECT MIC OUT", RED, BLACK);
    LCD_DrawString(8, 100, "THEN MEASURE PB0", WHITE, BLACK);
    LCD_DrawString(8, 140, "EXPECT ABOUT 3.3V", CYAN, BLACK);
    BSP_UART_SendString("PB0 HIGH TEST\r\n");
}
#endif

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
    HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */

  LCD_Init(0);
  BSP_ADC_Init();
  Buzzer_Init();
  BSP_UART_Init();
  LedBar_Init();

  /* 蜂鸣器自检 */
  Buzzer_On();
  LCD_DrawString(10, 150, "BUZZER TEST...", RED, BLACK);
  HAL_Delay(500);
  Buzzer_Off();

#if PB0_OUTPUT_TEST
    EnablePB0HighTestMode();
#else
  /* 默�?�麦克�?�源 */
  BSP_ADC_SetSource(AUDIO_SRC_MIC);
  BSP_ADC_Start();

  /* 读取拨码开关初始状�? */
  ReadDipSwitch();
    g_display_zoom = PotToDisplayZoom(BSP_ADC_ReadPot());
    AudioDisplay_SetHorizontalZoom(g_display_zoom);

  /* 显示菜单 */
  g_state = STATE_MENU;
  g_menu_cursor = 2;  /* 默�?�选中 Waveform */
  g_menu_prev_cursor = 0xFF;
  DrawMenu();

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
  g_led_last_tick = HAL_GetTick();
  g_dbg_update_tick = HAL_GetTick();
    g_spectrum_tx_tick = HAL_GetTick();
    g_status_tx_tick = HAL_GetTick();
  g_dip_last_tick = HAL_GetTick();
  g_pot_last_tick = HAL_GetTick();

  BSP_UART_SendString("Audio Visualizer Ready\r\n");
    BSP_UART_SendString("HELLO:STM32_AUDIO_VIS\r\n");
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

#if PB0_OUTPUT_TEST
    if ((now - g_led_last_tick) >= 500U) {
        g_led_last_tick = now;
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
    continue;
#endif

    KeyId_t key = ScanKey();

        ProcessBluetoothCommands();

    /* ===== 菜单状�? ===== */
    if (g_state == STATE_MENU) {
        switch (key) {
        case KEY_UP:
            if (g_menu_cursor > 0) g_menu_cursor--;
            else g_menu_cursor = MENU_ITEMS - 1;
            break;
        case KEY_DOWN:
            g_menu_cursor = (g_menu_cursor + 1) % MENU_ITEMS;
            break;
        case KEY_OK:
            MenuConfirm();
            break;
        default:
            break;
        }
        if (g_state == STATE_MENU) {
            UpdateMenuCursor();

            /* 定期读拨码开关并更新菜单显示 */
            if (!g_remote_fft_cfg && (now - g_dip_last_tick) >= 500) {
                g_dip_last_tick = now;
                ReadDipSwitch();
                DrawMenuConfigLine();
            }

            /* 读电位器 -> 显示模式�?��宽度 */
            if ((now - g_pot_last_tick) >= 100) {
                g_pot_last_tick = now;
                UpdateDisplayZoomFromPot();
            }

            /* 菜单里也持续采集, 更新RMS/TH */
            if (BSP_ADC_FrameReady()) {
                BSP_ADC_GetFrame(audio_frame);
                UpdateRawFrameStats(audio_frame, 256U);
                ApplyPowerNoiseFilter(audio_frame, 256U);
                ApplyMicAutoGain(audio_frame, 256U);
                if (g_enhanced_denoise_enabled) {
                    ApplyEnhancedDenoise(audio_frame, 256U);
                }
                g_last_rms = FFT_CalcRMS(audio_frame, 256);
                Buzzer_CheckAlarm(g_last_rms, g_alarm_threshold);
            }

            /* 定期刷新菜单底部RMS/TH */
            if ((now - g_dbg_update_tick) >= 200) {
                g_dbg_update_tick = now;
                g_mic_probe12 = BSP_ADC_ReadMicProbe();
                DrawMenuStatusLine();
                DrawMenuDiagLine();
            }
        }
    }
    /* ===== 显示状�? ===== */
    else {
        if (key == KEY_MENU) {
            g_state = STATE_MENU;
            g_menu_prev_cursor = 0xFF;
            DrawMenu();
        } else {
            /* 定期读拨�? */
            if (!g_remote_fft_cfg && (now - g_dip_last_tick) >= 300) {
                g_dip_last_tick = now;
                ReadDipSwitch();
            }

            /* 读电位器 �? 显示宽度 */
            if ((now - g_pot_last_tick) >= 100) {
                g_pot_last_tick = now;
                UpdateDisplayZoomFromPot();
            }

            /* ADC帧就�? */
            if (BSP_ADC_FrameReady()) {
                BSP_ADC_GetFrame(audio_frame);
                UpdateRawFrameStats(audio_frame, 256U);
                ApplyPowerNoiseFilter(audio_frame, 256U);
                ApplyMicAutoGain(audio_frame, 256U);
                if (g_enhanced_denoise_enabled) {
                    ApplyEnhancedDenoise(audio_frame, 256U);
                }
                AudioDisplay_Update(audio_frame, 256);

                g_last_rms = FFT_CalcRMS(audio_frame, 256);
                g_last_peak = CalcPeakLevel(audio_frame, 256);
                Buzzer_CheckAlarm(g_last_rms, g_alarm_threshold);

                /* 状态信�? + UART */
                if ((now - g_dbg_update_tick) >= 200) {
                    g_dbg_update_tick = now;
                    g_mic_probe12 = BSP_ADC_ReadMicProbe();

                    /* 底部状态条: 两�?? y=298, y=308 */
                    static const char * const wn[] = {"RECT","HAMM","HANN","BLKM"};

                    /* �?一�?: 输入�? + FFT配置 */
                    char ln1[32];
                    ln1[0] = '\0';
                    append_text(ln1, sizeof(ln1), SourceName(g_audio_src));
                    append_text(ln1, sizeof(ln1), " ");
                    append_uint(ln1, sizeof(ln1), g_fft_size);
                    append_text(ln1, sizeof(ln1), " ");
                    append_text(ln1, sizeof(ln1), wn[g_win_type]);
                    append_text(ln1, sizeof(ln1), " ");
                    append_uint(ln1, sizeof(ln1), g_display_zoom);
                    append_text(ln1, sizeof(ln1), "x");
                    LCD_FillRect(0, STATUS_Y, 240, 8, BLACK);
                    LCD_DrawString(4, STATUS_Y, ln1, CYAN, BLACK);

                    /* �?二�??: RMS + 阈�? + 报�?? */
                    char ln2[32];
                    ln2[0] = '\0';
                    append_text(ln2, sizeof(ln2), "RMS ");
                    append_uint(ln2, sizeof(ln2), g_last_rms);
                    append_text(ln2, sizeof(ln2), " TH ");
                    append_uint(ln2, sizeof(ln2), g_alarm_threshold);
                    if (g_last_rms > g_alarm_threshold) append_text(ln2, sizeof(ln2), " BUZZ");
                    LCD_FillRect(0, STATUS_Y + 9, 240, 8, BLACK);
                    LCD_DrawString(4, STATUS_Y + 9, ln2,
                        (g_last_rms > g_alarm_threshold) ? RED : GREEN, BLACK);
                    DrawDisplayDiagLine();

                }

                if ((now - g_spectrum_tx_tick) >= 500U) {
                    g_spectrum_tx_tick = now;
                    SendDisplayFrame(audio_frame, 256U);
                }
            }
        }
    }

    if ((now - g_status_tx_tick) >= 1500U) {
        g_status_tx_tick = now;
        SendStatusFrame();
    }

    /* LED�?�? */
    if ((now - g_led_last_tick) >= 500) {
        g_led_last_tick = now;
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }

    /* USER CODE END 3 */
  }
}

/**
  * @brief System Clock Configuration - 72MHz
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* HSI + PLL -> 64MHz (HSI=8MHz内部, PLL x16 /2 = 64MHz) */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;  /* 4MHz */
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;               /* 4*16=64MHz */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;      /* HCLK=64MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;        /* APB1=32MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;        /* APB2=64MHz */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 打开所有需要的GPIO时钟 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  /* 释放PB3/PB4/PA15的JTAG功能，保留SWD调试 */
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  /* ===== LED (PA6) ===== */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /* ===== 4�?导航按键: PB11(UP) PB12(DOWN) PA11(OK) PA12(MENU), 内部上拉 ===== */
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Pin = KEY_UP_Pin | KEY_DOWN_Pin;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = KEY_OK_Pin | KEY_MENU_Pin;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ===== LCD�?件SPI与控制引�?: PA0(SCL) PA1(SDA) PA2(RST) PA3(DC) PA4(CS) PA5(BLK) ===== */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ===== 蜂鸣�? PB10 ===== */
  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = BUZZER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);

  /* ===== 拨码开�? PB13/PB14/PB15/PA8: 输入上拉 ===== */
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Pin = DIP_SW1_Pin | DIP_SW2_Pin | DIP_SW3_Pin;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = DIP_SW4_Pin;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
