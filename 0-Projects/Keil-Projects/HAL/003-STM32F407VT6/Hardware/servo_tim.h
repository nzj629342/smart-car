/**
  ******************************************************************************
  * @file    servo_tim.h
  * @brief   舵机定时器时基（HAL 层）：TIM1 / TIM9 / TIM12 的 50Hz PWM 初始化
  * @note    写法与 CubeMX 生成的 tim.c 一致：
  *            MX_TIMx_Init() 里填 TIM_HandleTypeDef 并调 HAL_TIM_PWM_Init()；
  *            时钟与 GPIO 复用统一在 HAL_TIM_PWM_MspInit() 回调里处理。
  ******************************************************************************
  */

#ifndef __SERVO_TIM_H
#define __SERVO_TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 三路定时器句柄（由 servo_tim.c 定义，main.c 里直接 &htim1 使用） */
extern TIM_HandleTypeDef htim1;    /* CH1~CH4 -> PE9 / PE11 / PE13 / PE14 */
extern TIM_HandleTypeDef htim9;    /* CH1~CH2 -> PE5 / PE6              */
extern TIM_HandleTypeDef htim12;   /* CH1~CH2 -> PB14 / PB15            */

HAL_StatusTypeDef MX_TIM1_Init(void);    /* 50Hz，计数 1MHz */
HAL_StatusTypeDef MX_TIM9_Init(void);
HAL_StatusTypeDef MX_TIM12_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVO_TIM_H */
