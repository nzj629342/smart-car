/**
  ******************************************************************************
  * @file    servo.h
  * @brief   舵机业务层封装（HAL）：角度 ↔ 脉宽 ↔ CCR，运动状态机
  * @note    分工：
  *            servo_tim.c  —— 用 HAL 库初始化 TIM1/TIM9/TIM12 时基，
  *                            并在 HAL_TIM_PWM_MspInit() 里开时钟、配复用引脚；
  *            servo.c      —— 只管“一个舵机对象”的业务：脉宽映射、方向、
  *                            限速、定时 S 曲线，全部通过 HAL API 输出。
  *
  *          本工程 8 路通道：
  *            TIM1  CH1~CH4 -> PE9 / PE11 / PE13 / PE14 (AF1)
  *            TIM9  CH1~CH2 -> PE5 / PE6              (AF3)
  *            TIM12 CH1~CH2 -> PB14 / PB15             (AF9)
  ******************************************************************************
  */

#ifndef __SERVO_H
#define __SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Exported constants --------------------------------------------------------*/
#define SERVO_PWM_FREQ_HZ           50U        /* 舵机标准 PWM 频率 50Hz(20ms) */
#define SERVO_DEFAULT_PULSE_MIN_US  500.0f     /* 0.5ms 对应最小角度 */
#define SERVO_DEFAULT_PULSE_MAX_US  2500.0f    /* 2.5ms 对应最大角度 */
#define SERVO_CHANNEL_COUNT         8U         /* 本工程预留的舵机通道数 */

/* Exported types ------------------------------------------------------------*/

/** @brief 舵机运动状态 */
typedef enum
{
  SERVO_MODE_IDLE = 0,    /* 停止/保持 */
  SERVO_MODE_SPEED,       /* 按 speed_dps 限速运动 */
  SERVO_MODE_TIME         /* 按 move_time_ms 定时 S 曲线运动 */
} Servo_Mode_t;

/** @brief 舵机对象（一个实例控制一个舵机） */
typedef struct
{
  TIM_HandleTypeDef *htim;        /* HAL 定时器句柄，如 &htim1 / &htim9 / &htim12 */
  uint32_t           Channel;     /* 通道，如 TIM_CHANNEL_1 */

  float pulse_min_us;             /* 角度下限对应的脉宽 (us) */
  float pulse_max_us;             /* 角度上限对应的脉宽 (us) */
  float angle_min;                /* 机械行程下限 (°) */
  float angle_max;                /* 机械行程上限 (°) */

  uint8_t inverted;               /* 1 = 反装，角度→脉宽反向映射 */

  float angle;                    /* 当前输出角度 (°) */
  float target;                   /* 目标角度 (°) */
  float start_angle;              /* 本次运动起点角度 */
  float speed_dps;                /* 限速 (°/s)，0 表示不限速 */

  uint32_t move_start_ms;         /* 本次运动起始时刻(HAL_GetTick) */
  uint32_t move_time_ms;          /* 定时运动总时长 (ms) */
  uint32_t last_tick_ms;          /* 上次 Update 时刻 */

  float us_per_tick;              /* 定时器 1 个计数对应的微秒数 */

  Servo_Mode_t mode;              /* 当前运动模式 */
  uint8_t      attached;          /* 1 = 已绑定并已启动 PWM */
} Servo_t;

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief  读某个定时器在实际时钟配置下的输入频率（Hz）
 * @note   TIM1/TIM8/TIM9/TIM10/TIM11 走 APB2，其余走 APB1；
 *         APB 预分频≠1 时定时器时钟 = PCLK×2。servo_tim.c 算预分频要用它。
 */
uint32_t Servo_TIM_GetClockHz(TIM_TypeDef *inst);

/**
 * @brief  把一个已由 MX_TIMx_Init() 初始化的定时器通道绑定成舵机
 * @param  s            舵机对象
 * @param  htim         HAL 定时器句柄（内部已 HAL_TIM_PWM_Init 过）
 * @param  channel      TIM9 / TIM12 只有 CH1、CH2，绑 CH3/CH4 会返回 HAL_ERROR
 * @param  pulse_min_us 最小角度对应脉宽（不合法则用默认 500us）
 * @param  pulse_max_us 最大角度对应脉宽（不合法则用默认 2500us）
 * @param  angle_min    行程下限（如 0）
 * @param  angle_max    行程上限（如 180 / 270）
 * @retval HAL_OK / HAL_ERROR
 * @note   内部用 HAL_TIM_PWM_ConfigChannel() + HAL_TIM_PWM_Start()，
 *         初始化后停在行程中位，避免上电撞限位。
 */
HAL_StatusTypeDef Servo_Attach(Servo_t *s, TIM_HandleTypeDef *htim, uint32_t channel,
                               float pulse_min_us, float pulse_max_us,
                               float angle_min, float angle_max);

void Servo_Detach(Servo_t *s);                                 /* 停止该通道 PWM   */

void Servo_SetPulseRange(Servo_t *s, float pulse_min_us, float pulse_max_us);
void Servo_SetAngleRange(Servo_t *s, float angle_min, float angle_max);
void Servo_SetInverted(Servo_t *s, uint8_t inverted);          /* 1 = 反装        */
void Servo_SetSpeedLimit(Servo_t *s, float speed_dps);         /* 0 = 不限速      */

void  Servo_SetAngle(Servo_t *s, float angle);                 /* 立即到位        */
void  Servo_MoveTo(Servo_t *s, float angle);                   /* 按限速运动      */
void  Servo_MoveIn(Servo_t *s, float angle, uint32_t time_ms); /* 定时柔和到位    */
void  Servo_Update(Servo_t *s);                                /* 周期调用，1ms  */
void  Servo_UpdateAll(Servo_t *group, uint16_t count);         /* 数组版 Update   */

float Servo_GetAngle(const Servo_t *s);
uint8_t Servo_IsBusy(const Servo_t *s);                        /* 1 = 还在运动    */

void  Servo_SetPulseUs(Servo_t *s, float pulse_us);            /* 调试：直接给脉宽 */
float Servo_GetPulseUs(const Servo_t *s);                      /* 调试：读实际脉宽 */

#ifdef __cplusplus
}
#endif

#endif /* __SERVO_H */
