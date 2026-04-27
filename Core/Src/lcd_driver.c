#include "lcd_driver.h"
#include "stm32f1xx_hal.h"

extern SPI_HandleTypeDef hspi1;

/* LCD引脚定义 */
#define LCD_DC_HIGH()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET)
#define LCD_DC_LOW()    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET)
#define LCD_CS_HIGH()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)
#define LCD_CS_LOW()    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)

/* 写入命令 */
static void LCD_WriteCommand(uint8_t cmd)
{
    LCD_CS_LOW();
    LCD_DC_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    LCD_CS_HIGH();
}

/* 写入数据字节 */
static void LCD_WriteDataByte(uint8_t data)
{
    LCD_CS_LOW();
    LCD_DC_HIGH();
    HAL_SPI_Transmit(&hspi1, &data, 1, 100);
    LCD_CS_HIGH();
}

/* 初始化LCD */
void LCD_Init(void)
{
    /* ILI9341初始化序列 (简化版) */
    LCD_WriteCommand(0x01);  /* 软复位 */
    HAL_Delay(100);
    
    LCD_WriteCommand(0x28);  /* 显示关闭 */
    HAL_Delay(10);
    
    LCD_WriteCommand(0x29);  /* 显示打开 */
    HAL_Delay(10);
}

/* 设置光标位置 */
void LCD_SetCursor(uint16_t x, uint16_t y)
{
    /* 设置列地址 */
    LCD_WriteCommand(0x2A);
    LCD_WriteDataByte(x >> 8);
    LCD_WriteDataByte(x & 0xFF);
    
    /* 设置行地址 */
    LCD_WriteCommand(0x2B);
    LCD_WriteDataByte(y >> 8);
    LCD_WriteDataByte(y & 0xFF);
}

/* 填充屏幕颜色 */
void LCD_FillColor(uint16_t color)
{
    LCD_WriteCommand(0x2A);
    LCD_WriteDataByte(0);
    LCD_WriteDataByte(0);
    LCD_WriteDataByte(LCD_WIDTH >> 8);
    LCD_WriteDataByte(LCD_WIDTH & 0xFF);
    
    LCD_WriteCommand(0x2B);
    LCD_WriteDataByte(0);
    LCD_WriteDataByte(0);
    LCD_WriteDataByte(LCD_HEIGHT >> 8);
    LCD_WriteDataByte(LCD_HEIGHT & 0xFF);
    
    LCD_WriteCommand(0x2C);
    
    for(uint32_t i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++){
        LCD_WriteDataByte(color >> 8);
        LCD_WriteDataByte(color & 0xFF);
    }
}

/* 绘制像素点 */
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    LCD_SetCursor(x, y);
    LCD_WriteCommand(0x2C);
    LCD_WriteDataByte(color >> 8);
    LCD_WriteDataByte(color & 0xFF);
}

/* 绘制直线 */
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    
    if(dx == 0){
        /* 竖线 */
        for(int y = (y1 < y2) ? y1 : y2; y <= (y1 > y2) ? y1 : y2; y++){
            LCD_DrawPixel(x1, y, color);
        }
    } else if(dy == 0){
        /* 横线 */
        for(int x = (x1 < x2) ? x1 : x2; x <= (x1 > x2) ? x1 : x2; x++){
            LCD_DrawPixel(x, y1, color);
        }
    } else {
        /* Bresenham直线算法 */
        int steps = (dx > dy) ? dx : dy;
        float xs = (float)(x2 - x1) / steps;
        float ys = (float)(y2 - y1) / steps;
        
        for(int i = 0; i <= steps; i++){
            LCD_DrawPixel((int)(x1 + xs*i), (int)(y1 + ys*i), color);
        }
    }
}

/* 写入16位数据 */
void LCD_WriteData16(uint16_t data)
{
    LCD_WriteDataByte(data >> 8);
    LCD_WriteDataByte(data & 0xFF);
}
