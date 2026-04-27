#ifndef BSP_BLUETOOTH_H
#define BSP_BLUETOOTH_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* 蓝牙音频接收缓冲大小 */
#define BT_RX_BUF_SIZE    4096
#define BT_FRAME_SIZE     256     /* 一帧显示用的采样点数 */

/* 初始化蓝牙UART */
void BT_Init(UART_HandleTypeDef *huart);

/* 获取一帧可显示的音频数据，返回实际拷贝的采样点数 */
uint16_t BT_GetFrame(uint8_t *dest, uint16_t max_len);

/* 缓冲区中可用数据量 */
uint16_t BT_Available(void);

/* UART空闲中断回调（在stm32f1xx_it.c中调用） */
void BT_UART_IdleCallback(UART_HandleTypeDef *huart);

#endif
