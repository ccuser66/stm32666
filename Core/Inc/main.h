/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_Pin GPIO_PIN_6
#define LED_GPIO_Port GPIOA

/* ===== 4个导航按键 (内部上拉, 按下接地) ===== */
#define KEY_UP_Pin        GPIO_PIN_11
#define KEY_UP_Port       GPIOB
#define KEY_DOWN_Pin      GPIO_PIN_12
#define KEY_DOWN_Port     GPIOB
#define KEY_OK_Pin        GPIO_PIN_11
#define KEY_OK_Port       GPIOA
#define KEY_MENU_Pin      GPIO_PIN_12
#define KEY_MENU_Port     GPIOA

/* USER CODE BEGIN Private defines */

/* ===== 蜂鸣器 ===== */
#define BUZZER_Pin       GPIO_PIN_10
#define BUZZER_GPIO_Port GPIOB

/* ===== 电位器ADC ===== */
#define POT_Pin          GPIO_PIN_1     /* ADC1_CH9 */
#define POT_GPIO_Port    GPIOB

/* ===== 麦克风ADC ===== */
#define MIC_Pin          GPIO_PIN_0     /* ADC1_CH8 */
#define MIC_GPIO_Port    GPIOB

/* ===== 3.5mm音频ADC ===== */
#define AUDIO_IN_Pin     GPIO_PIN_7     /* ADC1_CH7 */
#define AUDIO_IN_GPIO_Port GPIOA

/* ===== 拨码开关 ===== */
#define DIP_SW1_Pin      GPIO_PIN_13    /* 窗函数低位 */
#define DIP_SW1_Port     GPIOB
#define DIP_SW2_Pin      GPIO_PIN_14    /* 窗函数高位 */
#define DIP_SW2_Port     GPIOB
#define DIP_SW3_Pin      GPIO_PIN_15    /* FFT点数低位 */
#define DIP_SW3_Port     GPIOB
#define DIP_SW4_Pin      GPIO_PIN_8     /* FFT点数高位 */
#define DIP_SW4_Port     GPIOA

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
