#include "audio_display.h"
#include "bsp_lcd.h"
#include "bsp_led_bar.h"
#include "fft.h"
#include "stm32f1xx_hal.h"
#include <string.h>

static uint16_t g_fft_mag[FFT_MAX_N / 2];

static DisplayMode_t display_mode = DISPLAY_MODE_TIME;

/* FFT参数 (由�?�部通过SetFFTConfig设置) */
static uint16_t  g_fft_size = 256;
static WindowType_t g_win_type = WIN_HAMMING;
static uint8_t g_horizontal_zoom = 1;

#define AUDIO_SAMPLE_RATE_HZ 8000U
#define AUDIO_FRAME_SAMPLES  256U

static uint8_t clamp_zoom(uint8_t zoom)
{
    if (zoom < 1U) return 1U;
    if (zoom > 4U) return 4U;
    return zoom;
}

static void label_uint_suffix(char *buf, uint16_t val, const char *suffix)
{
    char rev[5];
    uint8_t len = 0;
    uint8_t pos = 0;

    do {
        rev[len++] = (char)('0' + (val % 10U));
        val /= 10U;
    } while (val > 0U && len < sizeof(rev));

    while (len > 0U) {
        buf[pos++] = rev[--len];
    }
    while (*suffix != '\0') {
        buf[pos++] = *suffix++;
    }
    buf[pos] = '\0';
}

static uint16_t text_width_px(const char *text)
{
    return (uint16_t)(strlen(text) * 6U);
}

static uint16_t clamp_text_x(int32_t x, uint16_t width)
{
    if (x < 0) {
        return 0;
    }
    if ((uint32_t)x + width > LCD_W) {
        return (width >= LCD_W) ? 0U : (uint16_t)(LCD_W - width);
    }
    return (uint16_t)x;
}

static void draw_text_centered(uint16_t center_x, uint16_t y, const char *text)
{
    uint16_t width = text_width_px(text);
    uint16_t x = clamp_text_x((int32_t)center_x - (int32_t)(width / 2U), width);
    LCD_DrawString(x, y, text, GRAY, BLACK);
}

static void draw_text_right_aligned(uint16_t right_x, uint16_t y, const char *text)
{
    uint16_t width = text_width_px(text);
    uint16_t x = clamp_text_x((int32_t)right_x - (int32_t)width + 1, width);
    LCD_DrawString(x, y, text, GRAY, BLACK);
}

static uint16_t sample_to_wave_y(uint8_t sample)
{
    int32_t val = (int32_t)sample - 128;
    int32_t y = WAVE_MID - (val * (WAVE_H / 2) / 128);

    if (y < WAVE_Y + 1) y = WAVE_Y + 1;
    if (y > WAVE_Y + WAVE_H - 2) y = WAVE_Y + WAVE_H - 2;
    return (uint16_t)y;
}

static void reset_time_domain_trace(void)
{
    return;
}

static void clear_time_domain_column(uint16_t x)
{
    LCD_DrawVLine(x, WAVE_Y + 1, WAVE_H - 2, BLACK);
    LCD_DrawPixel(x, WAVE_MID, DARK_GRAY);
}

/* =========== 模式名称 =========== */
static const char * const mode_titles[] = {
    "WAVEFORM",
    "SPECTRUM",
    "WATERFALL",
    "VU METER"
};

static const uint16_t title_colors[] = { GREEN, CYAN, MAGENTA, YELLOW };

/* =========== 画坐标轴 =========== */
static void draw_axes_time(void)
{
    char label[8];
    uint16_t visible_ms = (uint16_t)((AUDIO_FRAME_SAMPLES * 1000U) /
                                     (AUDIO_SAMPLE_RATE_HZ * g_horizontal_zoom));

    /* Y�?: 左侧 */
    LCD_DrawVLine(WAVE_X, WAVE_Y, WAVE_H, GRAY);
    /* Y轴刻�?: +1, 0, -1 */
    LCD_DrawString(1, WAVE_Y,           "+1", GRAY, BLACK);
    LCD_DrawString(7, WAVE_MID - 4,      "0", GRAY, BLACK);
    LCD_DrawString(1, WAVE_Y+WAVE_H-10, "-1", GRAY, BLACK);
    /* �?�? */
    for (uint16_t x = WAVE_X; x < WAVE_X + WAVE_W; x += 4)
        LCD_DrawPixel(x, WAVE_MID, DARK_GRAY);

    /* X�?: 底部 */
    LCD_DrawHLine(WAVE_X, WAVE_Y + WAVE_H, WAVE_W, GRAY);
    /* X轴刻�?: 根据当前缩放显示�??时间 */
    LCD_DrawString(WAVE_X,             WAVE_Y+WAVE_H+2, "0",    GRAY, BLACK);
    label_uint_suffix(label, (uint16_t)(visible_ms / 4U), "ms");
    draw_text_centered((uint16_t)(WAVE_X + WAVE_W / 4U), WAVE_Y+WAVE_H+2, label);
    label_uint_suffix(label, (uint16_t)(visible_ms / 2U), "ms");
    draw_text_centered((uint16_t)(WAVE_X + WAVE_W / 2U), WAVE_Y+WAVE_H+2, label);
    label_uint_suffix(label, (uint16_t)((visible_ms * 3U) / 4U), "ms");
    draw_text_centered((uint16_t)(WAVE_X + (WAVE_W * 3U) / 4U), WAVE_Y+WAVE_H+2, label);
    label_uint_suffix(label, visible_ms, "ms");
    draw_text_right_aligned((uint16_t)(WAVE_X + WAVE_W - 1U), WAVE_Y+WAVE_H+2, label);
}

static void draw_axes_spectrum(void)
{
    char label[8];
    uint16_t max_freq_khz = (uint16_t)(((AUDIO_SAMPLE_RATE_HZ / 2U) / g_horizontal_zoom) / 1000U);

    /* Y�? */
    LCD_DrawVLine(WAVE_X, WAVE_Y, WAVE_H, GRAY);
    LCD_DrawString(1, WAVE_Y, "Mag", GRAY, BLACK);

    /* X�? */
    LCD_DrawHLine(WAVE_X, WAVE_Y + WAVE_H, WAVE_W, GRAY);
    LCD_DrawString(WAVE_X,            WAVE_Y+WAVE_H+2, "0",    GRAY, BLACK);
    label_uint_suffix(label, (uint16_t)(max_freq_khz / 2U), "kHz");
    draw_text_centered((uint16_t)(WAVE_X + WAVE_W / 2U), WAVE_Y+WAVE_H+2, label);
    label_uint_suffix(label, max_freq_khz, "kHz");
    draw_text_right_aligned((uint16_t)(WAVE_X + WAVE_W - 1U), WAVE_Y+WAVE_H+2, label);
}

/* =========== 通用背景 =========== */
static void draw_bg(void)
{
    LCD_Clear(BLACK);
    LCD_DrawString(60, 2, mode_titles[display_mode],
                   title_colors[display_mode], BLACK);

    LCD_DrawHLine(WAVE_X, WAVE_Y, WAVE_W, GRAY);
    LCD_DrawHLine(WAVE_X, WAVE_Y + WAVE_H - 1, WAVE_W, GRAY);

    /* 根据模式画坐标轴 */
    if (display_mode == DISPLAY_MODE_TIME)
    {
        reset_time_domain_trace();
        draw_axes_time();
    }
    else if (display_mode == DISPLAY_MODE_SPECTRUM)
        draw_axes_spectrum();
    else if (display_mode == DISPLAY_MODE_WATERFALL) {
        /* 瀑布�?: X�?=频率 */
        LCD_DrawHLine(WAVE_X, WAVE_Y + WAVE_H, WAVE_W, GRAY);
        LCD_DrawString(WAVE_X,            WAVE_Y+WAVE_H+2, "0",    GRAY, BLACK);
        LCD_DrawString(WAVE_X+WAVE_W-18,  WAVE_Y+WAVE_H+2, "4kHz", GRAY, BLACK);
    }

    /* 底部状态区: y=300~320, 由main.c负责绘制 */
}

/* =========== 时域波形 =========== */
static void update_time_domain(const uint8_t *data, uint16_t len)
{
    if (len < 2U) return;

    uint16_t visible_count = (uint16_t)(len / g_horizontal_zoom);
    uint16_t start_index;
    uint16_t prev_y = 0;

    if (visible_count < 2U) {
        visible_count = 2U;
    }
    if (visible_count > len) {
        visible_count = len;
    }

    start_index = (uint16_t)(len - visible_count);

    for (uint16_t column = 0; column < WAVE_W; column++) {
        uint16_t x = (uint16_t)(WAVE_X + column);
        uint16_t sample_offset = (uint16_t)(((uint32_t)column * (visible_count - 1U)) / (WAVE_W - 1U));
        uint16_t sample_index = (uint16_t)(start_index + (visible_count - 1U) - sample_offset);
        uint16_t cur_y = sample_to_wave_y(data[sample_index]);

        clear_time_domain_column(x);
        if (column == 0U) {
            LCD_DrawPixel(x, cur_y, GREEN);
        } else if (cur_y < prev_y) {
            LCD_DrawVLine(x, cur_y, (uint16_t)(prev_y - cur_y + 1U), GREEN);
        } else if (cur_y > prev_y) {
            LCD_DrawVLine(x, prev_y, (uint16_t)(cur_y - prev_y + 1U), GREEN);
        } else {
            LCD_DrawPixel(x, cur_y, GREEN);
        }

        prev_y = cur_y;
    }
}

/* =========== 频谱柱状�? =========== */
#define SPEC_BARS  30
#define BAR_W       7
#define BAR_GAP     1

static void update_spectrum_from_mag(void)
{
    uint16_t total_bins = g_fft_size / 2U;
    uint16_t visible_bins = (uint16_t)(total_bins / g_horizontal_zoom);
    uint16_t max_h = WAVE_H - 4;

    if (visible_bins < SPEC_BARS) {
        visible_bins = SPEC_BARS;
    }
    if (visible_bins > total_bins) {
        visible_bins = total_bins;
    }

    for (uint8_t k = 0; k < SPEC_BARS; k++) {
        uint32_t sum = 0;
        uint16_t start_bin = (uint16_t)(((uint32_t)k * visible_bins) / SPEC_BARS);
        uint16_t end_bin = (uint16_t)(((uint32_t)(k + 1U) * visible_bins) / SPEC_BARS);
        if (end_bin <= start_bin) {
            end_bin = (uint16_t)(start_bin + 1U);
        }
        if (end_bin > total_bins) {
            end_bin = total_bins;
        }

        for (uint16_t bin = start_bin; bin < end_bin; bin++) {
            sum += g_fft_mag[bin];
        }
        uint16_t avg = (uint16_t)(sum / (uint16_t)(end_bin - start_bin));

        uint16_t h = avg;
        if (h > max_h) h = max_h;

        uint16_t x0 = WAVE_X + 2 + (uint16_t)k * (BAR_W + BAR_GAP);
        if (x0 + BAR_W >= WAVE_X + WAVE_W - 1) continue;

        LCD_FillRect(x0, WAVE_Y + 1, BAR_W, WAVE_H - 2, BLACK);
        if (h > 0) {
            uint16_t y0 = WAVE_Y + WAVE_H - 2 - h;
            LCD_FillRect(x0, y0, BAR_W, h, CYAN);
        }
    }
}

/* =========== 瀑布�? =========== */
#define WF_ROWS  (WAVE_H - 2)

static uint16_t heatmap(uint16_t val)
{
    if (val > 255) val = 255;
    uint8_t v = (uint8_t)val;
    uint8_t r, g, b;

    if (v < 43) {
        r = 0; g = 0; b = (uint8_t)(v * 6);
    } else if (v < 86) {
        r = 0; g = (uint8_t)((v - 43) * 6); b = 255;
    } else if (v < 128) {
        r = 0; g = 255; b = (uint8_t)(255 - (v - 86) * 6);
    } else if (v < 170) {
        r = (uint8_t)((v - 128) * 6); g = 255; b = 0;
    } else if (v < 213) {
        r = 255; g = (uint8_t)(255 - (v - 170) * 6); b = 0;
    } else {
        r = 255; g = (uint8_t)((v - 213) * 6); b = (uint8_t)((v - 213) * 6);
    }

    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint16_t wf_row = 0;

static void update_waterfall_from_mag(void)
{
    uint16_t y = WAVE_Y + 1 + wf_row;

    LCD_SetWindow(WAVE_X, y, WAVE_X + WAVE_W - 1, y);
    LCD_WritePixels_Begin();
    for (uint16_t x = 0; x < WAVE_W; x++) {
        uint16_t bin = (uint32_t)x * (g_fft_size / 2) / WAVE_W;
        uint16_t color = heatmap(g_fft_mag[bin]);
        LCD_WritePixels_Data(color);
    }
    LCD_WritePixels_End();

    wf_row++;
    if (wf_row >= WF_ROWS) wf_row = 0;

    uint16_t next_y = WAVE_Y + 1 + wf_row;
    LCD_DrawHLine(WAVE_X, next_y, WAVE_W, WHITE);
}

/* =========== VU音量�? =========== */
static uint16_t vu_peak = 0;
static uint32_t vu_peak_tick = 0;

static uint32_t isqrt_u32(uint32_t x)
{
    if (x == 0) return 0;
    uint32_t r = x, q;
    while (1) {
        q = x / r;
        if (r <= q) return r;
        r = (r + q) >> 1;
    }
}

static void update_vu(const uint8_t *data, uint16_t len)
{
    if (len == 0) return;

    uint32_t sum_sq = 0;
    for (uint16_t i = 0; i < len; i++) {
        int16_t s = (int16_t)data[i] - 128;
        sum_sq += (uint32_t)(s * s);
    }
    uint16_t rms = (uint16_t)isqrt_u32(sum_sq / len);

    uint16_t max_h = WAVE_H - 20;
    uint16_t bar_h = (uint16_t)((uint32_t)rms * max_h / 128);
    if (bar_h > max_h) bar_h = max_h;

    /* 峰值保�? 1�? */
    uint32_t now = HAL_GetTick();
    if (bar_h >= vu_peak) {
        vu_peak = bar_h;
        vu_peak_tick = now;
    } else if ((now - vu_peak_tick) > 1000) {
        if (vu_peak > 0) vu_peak--;
        vu_peak_tick = now;
    }

    uint16_t bar_x = 60;
    uint16_t bar_w = 120;
    uint16_t bar_bottom = WAVE_Y + WAVE_H - 10;

    /* 擦除 */
    LCD_FillRect(bar_x, bar_bottom - max_h, bar_w, max_h, BLACK);

    /* �?-�?-红渐变柱 */
    if (bar_h > 0) {
        uint16_t green_limit  = max_h * 60 / 100;
        uint16_t yellow_limit = max_h * 85 / 100;

        /* 绿色�? */
        uint16_t green_h = (bar_h < green_limit) ? bar_h : green_limit;
        LCD_FillRect(bar_x, bar_bottom - green_h, bar_w, green_h, GREEN);

        /* 黄色�? */
        if (bar_h > green_limit) {
            uint16_t yellow_h = bar_h - green_limit;
            if (yellow_h > (yellow_limit - green_limit))
                yellow_h = yellow_limit - green_limit;
            LCD_FillRect(bar_x, bar_bottom - green_limit - yellow_h,
                         bar_w, yellow_h, YELLOW);
        }

        /* 红色�? */
        if (bar_h > yellow_limit) {
            uint16_t red_h = bar_h - yellow_limit;
            LCD_FillRect(bar_x, bar_bottom - bar_h, bar_w, red_h, RED);
        }
    }

    /* 峰值指示线 */
    if (vu_peak > 0) {
        LCD_DrawHLine(bar_x, bar_bottom - vu_peak, bar_w, WHITE);
    }

    /* 左侧刻度 */
    for (int pct = 0; pct <= 100; pct += 25) {
        uint16_t y = bar_bottom - (uint16_t)((uint32_t)pct * max_h / 100);
        LCD_DrawHLine(bar_x - 8, y, 6, GRAY);
    }

    /* 百分�? */
    uint16_t pct = (bar_h > 0) ? (uint16_t)((uint32_t)bar_h * 100 / max_h) : 0;
    char num[5];
    num[0] = '0' + (pct / 100) % 10;
    num[1] = '0' + (pct / 10) % 10;
    num[2] = '0' + pct % 10;
    num[3] = '%';
    num[4] = '\0';
    LCD_FillRect(bar_x + bar_w + 10, WAVE_MID - 4, 30, 8, BLACK);
    LCD_DrawString(bar_x + bar_w + 10, WAVE_MID - 4, num, WHITE, BLACK);
}

/* =========== �?共接�? =========== */
void AudioDisplay_Init(void)
{
    display_mode = DISPLAY_MODE_TIME;
    draw_bg();
}

void AudioDisplay_SetMode(DisplayMode_t mode)
{
    display_mode = mode;
    wf_row = 0;
    vu_peak = 0;
    draw_bg();
}

void AudioDisplay_Update(const uint8_t *data, uint16_t len)
{
    /* 每帧都做FFT，驱动LED频谱指示�? */
    FFT_Compute(data, g_fft_mag, g_fft_size, g_win_type);
    LedBar_Update(g_fft_mag, g_fft_size / 2);

    switch (display_mode) {
    case DISPLAY_MODE_TIME:      update_time_domain(data, len); break;
    case DISPLAY_MODE_SPECTRUM:  update_spectrum_from_mag();     break;
    case DISPLAY_MODE_WATERFALL: update_waterfall_from_mag();    break;
    case DISPLAY_MODE_VU:        update_vu(data, len);          break;
    default: break;
    }
}

void AudioDisplay_SetFFTConfig(uint16_t size, WindowType_t win)
{
    if (size != g_fft_size || win != g_win_type) {
        g_fft_size = size;
        g_win_type = win;
    }
}

void AudioDisplay_SetHorizontalZoom(uint8_t zoom)
{
    uint8_t next_zoom = clamp_zoom(zoom);
    if (next_zoom == g_horizontal_zoom) {
        return;
    }

    g_horizontal_zoom = next_zoom;
    if (display_mode == DISPLAY_MODE_TIME || display_mode == DISPLAY_MODE_SPECTRUM) {
        draw_bg();
    }
}

const uint16_t* AudioDisplay_GetFFTMag(uint16_t *num_bins)
{
    if (num_bins) *num_bins = g_fft_size / 2;
    return g_fft_mag;
}
