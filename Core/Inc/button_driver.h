#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <stdint.h>

/* 按键定义 */
typedef enum {
    BTN_MODE = 0,      // 模式切换
    BTN_SETTING = 1,   // 设置 (暂不用)
    BTN_HELP = 2,      // 帮助 (暂不用)
    BTN_COUNT = 3
} ButtonId;

/* 按键回调函数指针 */
typedef void (*ButtonCallback)(ButtonId btn);

/* 初始化按键系统 */
void Button_Init(void);

/* 按键扫描 (定期调用，如10ms) */
void Button_Scan(void);

/* 注册按键回调 */
void Button_RegisterCallback(ButtonId btn, ButtonCallback cb);

#endif /* BUTTON_DRIVER_H */
