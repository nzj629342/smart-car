#ifndef __SERVO_H
#define __SERVO_H

#include "stm32f10x.h"

// 舵机参数
#define SERVO_MIN_PULSE 500    // 0.5ms  单位：us
#define SERVO_MAX_PULSE 2500   // 2.5ms  单位：us
#define SERVO_MAX_ANGLE 180    // 最大角度

void Servo_Init(void);
void Servo0_SetAngle(uint16_t angle);
void Servo1_SetAngle(uint16_t angle);

// 非阻塞平滑启动：设置目标角度、每步间隔ms
void Servo0_SmoothStart(uint16_t targetAngle, uint16_t stepMs);
void Servo1_SmoothStart(uint16_t targetAngle, uint16_t stepMs);
// 放在主循环里持续调用，刷新舵机
void Servo_SmoothTask(void);

#endif
