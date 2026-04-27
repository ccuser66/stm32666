#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#include <stdint.h>

void Buzzer_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);

/* 根据音频RMS幅度和阈值自动控制蜂鸣器
 * rms: 当前帧RMS (0-128)
 * threshold: 报警阈值 (0-128), 由电位器控制
 */
void Buzzer_CheckAlarm(uint16_t rms, uint16_t threshold);

#endif
