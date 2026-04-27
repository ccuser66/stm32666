#include "button_driver.h"
#include "stm32f1xx_hal.h"

/* 按键结构体 */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint8_t state;       /* 0: 未按, 1: 按下 */
    uint16_t cnt;        /* 去抖计数 */
    ButtonCallback callback;
} Button;

/* 按键配置表 */
static Button buttons[BTN_COUNT] = {
    {GPIOB, GPIO_PIN_0, 0, 0, NULL},    /* BTN_MODE */
    {GPIOB, GPIO_PIN_1, 0, 0, NULL},    /* BTN_SETTING */
    {GPIOB, GPIO_PIN_10, 0, 0, NULL},   /* BTN_HELP */
};

/* 初始化按键系统 */
void Button_Init(void)
{
    /* GPIO已在CubeMX中配置 */
}

/* 按键扫描 (定期调用，如10ms) */
void Button_Scan(void)
{
    for(int i = 0; i < BTN_COUNT; i++){
        /* 读取GPIO引脚状态 (0=按下, 1=释放) */
        uint8_t pin_state = !HAL_GPIO_ReadPin(buttons[i].port, buttons[i].pin);
        
        if(pin_state){  /* 按键按下 */
            buttons[i].cnt++;
            /* 去抖20ms后，如果之前是释放状态 */
            if(buttons[i].cnt >= DEBOUNCE_TIME_MS && buttons[i].state == 0){
                buttons[i].state = 1;  /* 标记为按下 */
                if(buttons[i].callback){
                    buttons[i].callback(i);  /* 调用回调函数 */
                }
            }
        } else {  /* 按键释放 */
            buttons[i].cnt = 0;
            buttons[i].state = 0;
        }
    }
}

/* 注册按键回调 */
void Button_RegisterCallback(ButtonId btn, ButtonCallback cb)
{
    if(btn < BTN_COUNT){
        buttons[btn].callback = cb;
    }
}
