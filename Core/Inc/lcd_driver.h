#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>
#include "config.h"

/* 颜色定义 (RGB565) */
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

/* 初始化LCD */
void LCD_Init(void);

/* 设置光标位置 */
void LCD_SetCursor(uint16_t x, uint16_t y);

/* 填充屏幕颜色 */
void LCD_FillColor(uint16_t color);

/* 绘制像素点 */
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

/* 绘制直线 */
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/* 写入数据 */
void LCD_WriteData16(uint16_t data);

#endif /* LCD_DRIVER_H */
