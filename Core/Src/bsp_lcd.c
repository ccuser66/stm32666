#include "bsp_lcd.h"
#include <string.h>

/* =========== 引脚定义 =========== */
#define LCD_SCL_PORT  GPIOA
#define LCD_SCL_PIN   GPIO_PIN_0
#define LCD_SDA_PORT  GPIOA
#define LCD_SDA_PIN   GPIO_PIN_1
#define LCD_CS_PORT   GPIOA
#define LCD_CS_PIN    GPIO_PIN_4
#define LCD_DC_PORT   GPIOA
#define LCD_DC_PIN    GPIO_PIN_3
#define LCD_RST_PORT  GPIOA
#define LCD_RST_PIN   GPIO_PIN_2

/* 直接寄存器操作，比HAL_GPIO_WritePin快3-5倍 */
#define SCL_LOW()   (LCD_SCL_PORT->BRR  = LCD_SCL_PIN)
#define SCL_HIGH()  (LCD_SCL_PORT->BSRR = LCD_SCL_PIN)
#define SDA_LOW()   (LCD_SDA_PORT->BRR  = LCD_SDA_PIN)
#define SDA_HIGH()  (LCD_SDA_PORT->BSRR = LCD_SDA_PIN)
#define CS_LOW()    (LCD_CS_PORT->BRR   = LCD_CS_PIN)
#define CS_HIGH()   (LCD_CS_PORT->BSRR  = LCD_CS_PIN)
#define DC_CMD()    (LCD_DC_PORT->BRR   = LCD_DC_PIN)
#define DC_DATA()   (LCD_DC_PORT->BSRR  = LCD_DC_PIN)
#define RST_LOW()   (LCD_RST_PORT->BRR  = LCD_RST_PIN)
#define RST_HIGH()  (LCD_RST_PORT->BSRR = LCD_RST_PIN)

static SPI_HandleTypeDef *lcd_spi;

/* SPI块传输缓冲，用于加速填充 */
#define SPI_BUF_SIZE  512
static uint8_t spi_buf[SPI_BUF_SIZE];

static void SW_SPI_WriteByte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++) {
        SCL_LOW();
        if (data & 0x80) {
            SDA_HIGH();
        } else {
            SDA_LOW();
        }
        data <<= 1;
        SCL_HIGH();
    }
}

/* =========== 底层SPI读写 =========== */
static void LCD_WriteCmd(uint8_t cmd)
{
    CS_LOW();
    DC_CMD();
    SW_SPI_WriteByte(cmd);
    CS_HIGH();
}

static void LCD_WriteData8(uint8_t data)
{
    CS_LOW();
    DC_DATA();
    SW_SPI_WriteByte(data);
    CS_HIGH();
}

static void LCD_WriteData16(uint16_t data)
{
    CS_LOW();
    DC_DATA();
    SW_SPI_WriteByte((uint8_t)(data >> 8));
    SW_SPI_WriteByte((uint8_t)(data & 0xFF));
    CS_HIGH();
}

/* =========== ILI9341初始化序列 =========== */
void LCD_Init(SPI_HandleTypeDef *hspi)
{
    lcd_spi = hspi;

    /* 硬件复位 */
    CS_HIGH();
    RST_HIGH();
    HAL_Delay(5);
    RST_LOW();
    HAL_Delay(20);
    RST_HIGH();
    HAL_Delay(150);

    /* 软件复位 */
    LCD_WriteCmd(0x01);
    HAL_Delay(150);

    /* 关闭休眠 */
    LCD_WriteCmd(0x11);
    HAL_Delay(150);

    /* ---- 电源控制 (ILI9341必须) ---- */
    LCD_WriteCmd(0xCF);  /* Power control B */
    LCD_WriteData8(0x00);
    LCD_WriteData8(0xC1);
    LCD_WriteData8(0x30);

    LCD_WriteCmd(0xED);  /* Power on sequence control */
    LCD_WriteData8(0x64);
    LCD_WriteData8(0x03);
    LCD_WriteData8(0x12);
    LCD_WriteData8(0x81);

    LCD_WriteCmd(0xE8);  /* Driver timing control A */
    LCD_WriteData8(0x85);
    LCD_WriteData8(0x00);
    LCD_WriteData8(0x78);

    LCD_WriteCmd(0xCB);  /* Power control A */
    LCD_WriteData8(0x39);
    LCD_WriteData8(0x2C);
    LCD_WriteData8(0x00);
    LCD_WriteData8(0x34);
    LCD_WriteData8(0x02);

    LCD_WriteCmd(0xF7);  /* Pump ratio control */
    LCD_WriteData8(0x20);

    LCD_WriteCmd(0xEA);  /* Driver timing control B */
    LCD_WriteData8(0x00);
    LCD_WriteData8(0x00);

    LCD_WriteCmd(0xC0);  /* Power Control 1 */
    LCD_WriteData8(0x23);

    LCD_WriteCmd(0xC1);  /* Power Control 2 */
    LCD_WriteData8(0x10);

    LCD_WriteCmd(0xC5);  /* VCOM Control 1 */
    LCD_WriteData8(0x3E);
    LCD_WriteData8(0x28);

    LCD_WriteCmd(0xC7);  /* VCOM Control 2 */
    LCD_WriteData8(0x86);

    /* ---- 显示参数 ---- */
    LCD_WriteCmd(0x36);  /* Memory Access Control */
    LCD_WriteData8(0x48); /* MY=0, MX=1, MV=0, BGR=1 */

    LCD_WriteCmd(0x3A);  /* Pixel Format: 16bit RGB565 */
    LCD_WriteData8(0x55);

    LCD_WriteCmd(0xB1);  /* Frame Rate Control */
    LCD_WriteData8(0x00);
    LCD_WriteData8(0x18); /* 79Hz */

    LCD_WriteCmd(0xB6);  /* Display Function Control */
    LCD_WriteData8(0x08);
    LCD_WriteData8(0x82);
    LCD_WriteData8(0x27);

    LCD_WriteCmd(0xF2);  /* 3Gamma Function Disable */
    LCD_WriteData8(0x00);

    LCD_WriteCmd(0x26);  /* Gamma Set */
    LCD_WriteData8(0x01);

    /* ---- Gamma校正 ---- */
    LCD_WriteCmd(0xE0);  /* Positive Gamma */
    LCD_WriteData8(0x0F); LCD_WriteData8(0x31); LCD_WriteData8(0x2B);
    LCD_WriteData8(0x0C); LCD_WriteData8(0x0E); LCD_WriteData8(0x08);
    LCD_WriteData8(0x4E); LCD_WriteData8(0xF1); LCD_WriteData8(0x37);
    LCD_WriteData8(0x07); LCD_WriteData8(0x10); LCD_WriteData8(0x03);
    LCD_WriteData8(0x0E); LCD_WriteData8(0x09); LCD_WriteData8(0x00);

    LCD_WriteCmd(0xE1);  /* Negative Gamma */
    LCD_WriteData8(0x00); LCD_WriteData8(0x0E); LCD_WriteData8(0x14);
    LCD_WriteData8(0x03); LCD_WriteData8(0x11); LCD_WriteData8(0x07);
    LCD_WriteData8(0x31); LCD_WriteData8(0xC1); LCD_WriteData8(0x48);
    LCD_WriteData8(0x08); LCD_WriteData8(0x0F); LCD_WriteData8(0x0C);
    LCD_WriteData8(0x31); LCD_WriteData8(0x36); LCD_WriteData8(0x0F);

    /* Column / Page Address */
    LCD_WriteCmd(0x2A);
    LCD_WriteData8(0x00); LCD_WriteData8(0x00);
    LCD_WriteData8(0x00); LCD_WriteData8(0xEF);

    LCD_WriteCmd(0x2B);
    LCD_WriteData8(0x00); LCD_WriteData8(0x00);
    LCD_WriteData8(0x01); LCD_WriteData8(0x3F);

    /* Display ON */
    LCD_WriteCmd(0x29);
    HAL_Delay(50);
}

/* =========== 设置写入窗口 =========== */
void LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    LCD_WriteCmd(0x2A);
    LCD_WriteData8(x1 >> 8); LCD_WriteData8(x1 & 0xFF);
    LCD_WriteData8(x2 >> 8); LCD_WriteData8(x2 & 0xFF);

    LCD_WriteCmd(0x2B);
    LCD_WriteData8(y1 >> 8); LCD_WriteData8(y1 & 0xFF);
    LCD_WriteData8(y2 >> 8); LCD_WriteData8(y2 & 0xFF);

    LCD_WriteCmd(0x2C);  /* Memory Write */
}

/* =========== 填充整屏 =========== */
void LCD_Clear(uint16_t color)
{
    LCD_SetWindow(0, 0, LCD_W - 1, LCD_H - 1);

    /* 用块传输加速：先填满缓冲再批量发送 */
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    for (uint16_t i = 0; i < SPI_BUF_SIZE; i += 2) {
        spi_buf[i]   = hi;
        spi_buf[i+1] = lo;
    }

    uint32_t total = (uint32_t)LCD_W * LCD_H * 2;
    CS_LOW();
    DC_DATA();
    while (total > 0) {
        uint16_t chunk = (total > SPI_BUF_SIZE) ? SPI_BUF_SIZE : total;
        for (uint16_t i = 0; i < chunk; i++) {
            SW_SPI_WriteByte(spi_buf[i]);
        }
        total -= chunk;
    }
    CS_HIGH();
}

/* =========== 画像素 =========== */
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    LCD_SetWindow(x, y, x, y);
    LCD_WriteData16(color);
}

/* =========== 画竖线 =========== */
void LCD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    if (y + h > LCD_H) h = LCD_H - y;
    LCD_SetWindow(x, y, x, y + h - 1);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    CS_LOW();
    DC_DATA();
    for (uint16_t i = 0; i < h; i++) {
        SW_SPI_WriteByte(hi);
        SW_SPI_WriteByte(lo);
    }
    CS_HIGH();
}

/* =========== 画横线 =========== */
void LCD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    if (x + w > LCD_W) w = LCD_W - x;
    LCD_SetWindow(x, y, x + w - 1, y);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    CS_LOW();
    DC_DATA();
    for (uint16_t i = 0; i < w; i++) {
        SW_SPI_WriteByte(hi);
        SW_SPI_WriteByte(lo);
    }
    CS_HIGH();
}

/* =========== 填充矩形 =========== */
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;

    LCD_SetWindow(x, y, x + w - 1, y + h - 1);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    for (uint16_t i = 0; i < SPI_BUF_SIZE; i += 2) {
        spi_buf[i] = hi;
        spi_buf[i+1] = lo;
    }

    uint32_t total = (uint32_t)w * h * 2;
    CS_LOW();
    DC_DATA();
    while (total > 0) {
        uint16_t chunk = (total > SPI_BUF_SIZE) ? SPI_BUF_SIZE : total;
        for (uint16_t i = 0; i < chunk; i++) {
            SW_SPI_WriteByte(spi_buf[i]);
        }
        total -= chunk;
    }
    CS_HIGH();
}

/* =========== 简单5×7字体 =========== */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x00,0x08,0x14,0x22,0x41}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x41,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x01,0x01}, /* F */
    {0x3E,0x41,0x41,0x51,0x32}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x7F,0x20,0x18,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
};

void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg)
{
    if (c < ' ' || c > 'Z') c = ' ';
    int idx = c - ' ';

    /* 设置 6x7 窗口，一次性写入所有像素（比逐像素DrawPixel快5倍） */
    LCD_SetWindow(x, y, x + 5, y + 6);

    uint8_t c_hi = color >> 8, c_lo = color & 0xFF;
    uint8_t b_hi = bg >> 8,    b_lo = bg & 0xFF;

    CS_LOW();
    DC_DATA();
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (font5x7[idx][col] & (1 << row)) {
                SW_SPI_WriteByte(c_hi);
                SW_SPI_WriteByte(c_lo);
            } else {
                SW_SPI_WriteByte(b_hi);
                SW_SPI_WriteByte(b_lo);
            }
        }
        /* 字间距1列: 背景色 */
        SW_SPI_WriteByte(b_hi);
        SW_SPI_WriteByte(b_lo);
    }
    CS_HIGH();
}

void LCD_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg)
{
    while (*str) {
        if (*str >= 'a' && *str <= 'z') {
            LCD_DrawChar(x, y, *str - 32, color, bg);  /* 转大写 */
        } else {
            LCD_DrawChar(x, y, *str, color, bg);
        }
        x += 6;
        if (x + 6 > LCD_W) { x = 0; y += 8; }
        str++;
    }
}

/* =========== 流式像素写入（瀑布图用） =========== */
void LCD_WritePixels_Begin(void)
{
    CS_LOW();
    DC_DATA();
}

void LCD_WritePixels_Data(uint16_t color)
{
    SW_SPI_WriteByte((uint8_t)(color >> 8));
    SW_SPI_WriteByte((uint8_t)(color & 0xFF));
}

void LCD_WritePixels_End(void)
{
    CS_HIGH();
}
