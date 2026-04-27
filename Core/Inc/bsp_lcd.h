#ifndef BSP_LCD_H
#define BSP_LCD_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* =========== 屏幕参数 =========== */
#define LCD_W  240
#define LCD_H  320

/* =========== RGB565颜色 =========== */
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define DARKGREEN  0x03E0
#define GRAY    0x7BEF
#define DARK_GRAY  0x39E7

/* 初始化LCD */
void LCD_Init(SPI_HandleTypeDef *hspi);

/* 设置显示窗口 */
void LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/* 填充整屏 */
void LCD_Clear(uint16_t color);

/* 画一个像素 */
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

/* 画竖线 (快速，用于频谱柱) */
void LCD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);

/* 画横线 */
void LCD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

/* 填充矩形 */
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* 显示一个ASCII字符 (8×16) */
void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg);

/* 显示字符串 */
void LCD_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg);

/* 流式写入像素 (用于瀑布图等逐行绘制) */
void LCD_WritePixels_Begin(void);
void LCD_WritePixels_Data(uint16_t color);
void LCD_WritePixels_End(void);

#endif
