#include "ui_renderer.h"
#include "lcd_driver.h"

static DisplayMode current_mode = MODE_TIME_DOMAIN;

/* 初始化UI系统 */
void UI_Init(void)
{
    LCD_Init();
    LCD_FillColor(COLOR_BLACK);
    current_mode = MODE_TIME_DOMAIN;
}

/* 渲染时域波形 */
void UI_RenderTimeDomain(int16_t* data, uint16_t size)
{
    /* 清屏 */
    LCD_FillColor(COLOR_BLACK);
    
    /* 绘制波形
       将2048个点映射到240像素宽度 */
    for(int i = 0; i < 239; i++){
        int idx1 = (i * size) / 240;
        int idx2 = ((i+1) * size) / 240;
        
        /* 转换到屏幕Y坐标 (160为中心) */
        int y1 = 160 - (data[idx1] >> 8);
        int y2 = 160 - (data[idx2] >> 8);
        
        /* 限制在屏幕范围内 */
        if(y1 < 0) y1 = 0;
        if(y1 > LCD_HEIGHT-1) y1 = LCD_HEIGHT-1;
        if(y2 < 0) y2 = 0;
        if(y2 > LCD_HEIGHT-1) y2 = LCD_HEIGHT-1;
        
        /* 绘制线段 */
        LCD_DrawLine(i, y1, i+1, y2, COLOR_GREEN);
    }
}

/* 渲染频域频谱 */
void UI_RenderFrequency(FFTResult* fft_result)
{
    /* 清屏 */
    LCD_FillColor(COLOR_BLACK);
    
    /* 绘制频谱柱状图 */
    int num_bins = 60;  /* 分成60个频点 */
    
    for(int i = 0; i < num_bins; i++){
        int bin_idx = (i * FFT_SIZE/2) / num_bins;
        
        /* 幅度值映射到高度 (0-300) */
        float mag = fft_result->magnitude[bin_idx];
        int height = (int)(mag / 80.0f * 300.0f);
        if(height > 300) height = 300;
        if(height < 0) height = 0;
        
        /* 绘制柱子 (宽度4像素) */
        int x = i * 4;
        int y_bottom = 310;
        
        for(int w = 0; w < 4; w++){
            LCD_DrawLine(x + w, y_bottom, x + w, y_bottom - height, COLOR_CYAN);
        }
    }
}

/* 切换显示模式 */
void UI_SwapMode(void)
{
    current_mode = (current_mode + 1) % MODE_COUNT;
}

/* 获取当前模式 */
DisplayMode UI_GetCurrentMode(void)
{
    return current_mode;
}
