#include "bluetooth_uart.h"
#include "config.h"
#include "stm32f1xx_hal.h"

/* 环形缓冲结构体 */
typedef struct {
    uint8_t buffer[BT_BUFFER_SIZE];
    uint16_t write_idx;
    uint16_t read_idx;
    uint16_t count;
} BT_CircularBuffer;

static BT_CircularBuffer bt_buf = {0};
extern UART_HandleTypeDef huart1;

/* 初始化蓝牙模块 */
void BT_Init(void)
{
    bt_buf.write_idx = 0;
    bt_buf.read_idx = 0;
    bt_buf.count = 0;
}

/* UART中断回调 - HAL库会自动调用此函数 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART1){
        /* 获取接收的数据 */
        uint8_t byte = huart->pRxBuffPtr[-1];
        
        /* 写入环形缓冲 */
        bt_buf.buffer[bt_buf.write_idx++] = byte;
        if(bt_buf.write_idx >= BT_BUFFER_SIZE)
            bt_buf.write_idx = 0;
        
        if(++bt_buf.count >= BT_BUFFER_SIZE)
            bt_buf.count = BT_BUFFER_SIZE - 1;  /* 防止溢出 */
    }
}

/* 获取缓冲中的数据数量 */
uint16_t BT_GetDataCount(void)
{
    return bt_buf.count;
}

/* 从缓冲中读取数据 */
uint16_t BT_ReadData(uint8_t* dest, uint16_t len)
{
    uint16_t read_len = (len > bt_buf.count) ? bt_buf.count : len;
    
    for(uint16_t i = 0; i < read_len; i++){
        dest[i] = bt_buf.buffer[bt_buf.read_idx++];
        if(bt_buf.read_idx >= BT_BUFFER_SIZE)
            bt_buf.read_idx = 0;
    }
    
    bt_buf.count -= read_len;
    return read_len;
}

/* 清空缓冲 */
void BT_ClearBuffer(void)
{
    bt_buf.write_idx = 0;
    bt_buf.read_idx = 0;
    bt_buf.count = 0;
}
