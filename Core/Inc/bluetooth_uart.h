#ifndef BLUETOOTH_UART_H
#define BLUETOOTH_UART_H

#include <stdint.h>
#include <stdbool.h>

/* 初始化蓝牙模块 */
void BT_Init(void);

/* 获取缓冲中的数据数量 */
uint16_t BT_GetDataCount(void);

/* 从缓冲中读取数据 */
uint16_t BT_ReadData(uint8_t* dest, uint16_t len);

/* 清空缓冲 */
void BT_ClearBuffer(void);

#endif /* BLUETOOTH_UART_H */
