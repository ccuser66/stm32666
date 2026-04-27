#include "bsp_buzzer.h"
#include "main.h"

void Buzzer_Init(void)
{
    /* PB10已在MX_GPIO_Init中配置为输出 */
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
}

void Buzzer_On(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
}

void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
}

void Buzzer_CheckAlarm(uint16_t rms, uint16_t threshold)
{
    if (rms > threshold) {
        Buzzer_On();
    } else {
        Buzzer_Off();
    }
}
