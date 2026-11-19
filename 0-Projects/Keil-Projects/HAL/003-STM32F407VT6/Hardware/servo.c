/**
  ******************************************************************************
  * @file    servo.c
  * @brief   舵机业务层实现（HAL）：只做角度/脉宽换算与运动规划，
  *          寄存器操作全部交给 HAL 库（HAL_TIM_PWM_ConfigChannel /
  *          HAL_TIM_PWM_Start / __HAL_TIM_SET_COMPARE）。
  * @note    硬件层（时钟、GPIO 复用）在 servo_tim.c 的 HAL_TIM_PWM_MspInit 里。
  ******************************************************************************
  */

#include "servo.h"
#include <string.h>

/* ============================ 内部工具函数 ============================ */

/* 浮点限幅 */
static float Servo_Clamp(float v, float lo, float hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

/* 该定时器在本工程里是否真的存在这个通道 */
static uint8_t Servo_IsChannelValid(TIM_TypeDef *inst, uint32_t channel)
{
  if ((channel == TIM_CHANNEL_1) || (channel == TIM_CHANNEL_2))
  {
    return 1U;                       /* 所有用到的定时器都有 CH1/CH2 */
  }
  if ((channel == TIM_CHANNEL_3) || (channel == TIM_CHANNEL_4))
  {
    if ((inst == TIM1) || (inst == TIM2) || (inst == TIM3) ||
        (inst == TIM4) || (inst == TIM5) || (inst == TIM8))
    {
      return 1U;                     /* 这四个通道的定时器才有 CH3/CH4 */
    }
    return 0U;                       /* TIM9 / TIM12 只有两个通道 */
  }
  return 0U;
}

/* 角度 → 脉宽（考虑倒装） */
static float Servo_AngleToPulse(const Servo_t *s, float angle)
{
  float t;
  float a;

  if (s->angle_max <= s->angle_min)
  {
    return s->pulse_min_us;
  }

  a = Servo_Clamp(angle, s->angle_min, s->angle_max);
  t = (a - s->angle_min) / (s->angle_max - s->angle_min);   /* 0~1 */

  if (s->inverted != 0U)
  {
    t = 1.0f - t;                                           /* 反装：镜像 */
  }

  return s->pulse_min_us + (t * (s->pulse_max_us - s->pulse_min_us));
}

/* 脉宽 → CCR，用 HAL 宏写比较寄存器 */
static void Servo_WritePulse(Servo_t *s, float pulse_us)
{
  uint32_t ccr;
  uint32_t arr;

  if ((s->htim == 0) || (s->us_per_tick <= 0.0f))
  {
    return;
  }

  arr = s->htim->Init.Period + 1U;
  if (arr == 0U)
  {
    return;
  }

  ccr = (uint32_t)((pulse_us / s->us_per_tick) + 0.5f);
  if (ccr >= arr)
  {
    ccr = arr - 1U;             /* 不允许脉宽吃满整个周期 */
  }

  __HAL_TIM_SET_COMPARE(s->htim, s->Channel, ccr);
}

/* 输出某个角度 */
static void Servo_WriteAngle(Servo_t *s, float angle)
{
  Servo_WritePulse(s, Servo_AngleToPulse(s, angle));
}

/* ============================ 对外接口 ============================ */

/**
 * @brief  读定时器输入时钟（Hz）
 */
uint32_t Servo_TIM_GetClockHz(TIM_TypeDef *inst)
{
  RCC_ClkInitTypeDef clkcfg = {0};
  uint32_t latency = 0U;
  uint32_t pclk;
  uint32_t div;

  HAL_RCC_GetClockConfig(&clkcfg, &latency);

  /* F407：TIM1 / TIM8 / TIM9 / TIM10 / TIM11 挂 APB2，其余挂 APB1 */
  if ((inst == TIM1) || (inst == TIM8) || (inst == TIM9))
  {
    pclk = HAL_RCC_GetPCLK2Freq();
    div  = clkcfg.APB2CLKDivider;
  }
  else
  {
    pclk = HAL_RCC_GetPCLK1Freq();
    div  = clkcfg.APB1CLKDivider;
  }

  return (div == RCC_HCLK_DIV1) ? pclk : (pclk * 2U);
}

/**
 * @brief  绑定通道并启动 PWM（HAL 接口）
 */
HAL_StatusTypeDef Servo_Attach(Servo_t *s, TIM_HandleTypeDef *htim, uint32_t channel,
                               float pulse_min_us, float pulse_max_us,
                               float angle_min, float angle_max)
{
  TIM_OC_InitTypeDef oc = {0};
  uint32_t timclk;
  uint32_t psc_reg;
  float    center_angle;

  if ((s == 0) || (htim == 0) || (htim->Instance == 0))
  {
    return HAL_ERROR;     /* 没先调 MX_TIMx_Init() 就会走到这里 */
  }
  if (Servo_IsChannelValid(htim->Instance, channel) == 0U)
  {
    return HAL_ERROR;
  }

  /* 参数容错：不合法的映射回落到标准 0.5~2.5ms */
  if (pulse_min_us <= 0.0f)
  {
    pulse_min_us = SERVO_DEFAULT_PULSE_MIN_US;
  }
  if (pulse_max_us <= pulse_min_us)
  {
    pulse_max_us = SERVO_DEFAULT_PULSE_MAX_US;
  }
  if (angle_max <= angle_min)
  {
    angle_min = 0.0f;
    angle_max = 180.0f;
  }

  memset(s, 0, sizeof(Servo_t));
  s->htim          = htim;
  s->Channel       = channel;
  s->pulse_min_us  = pulse_min_us;
  s->pulse_max_us  = pulse_max_us;
  s->angle_min     = angle_min;
  s->angle_max     = angle_max;
  s->inverted      = 0U;
  s->speed_dps     = 0.0f;              /* 默认不限速 */
  s->mode          = SERVO_MODE_IDLE;
  s->last_tick_ms  = HAL_GetTick();

  /* 1 tick 对应多少 us（时基由 servo_tim.c 配成 1MHz） */
  timclk  = Servo_TIM_GetClockHz(htim->Instance);
  psc_reg = (uint32_t)htim->Init.Prescaler + 1U;
  s->us_per_tick = ((float)psc_reg * 1000000.0f) / (float)timclk;

  /* 上电停在行程中位，避免一头撞到限位 */
  center_angle = (s->angle_min + s->angle_max) * 0.5f;
  s->angle     = center_angle;
  s->target    = center_angle;

  oc.OCMode       = TIM_OCMODE_PWM1;
  oc.Pulse        = (uint32_t)((Servo_AngleToPulse(s, center_angle) / s->us_per_tick) + 0.5f);
  oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
  oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  oc.OCFastMode   = TIM_OCFAST_DISABLE;
  oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
  oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

  if (HAL_TIM_PWM_ConfigChannel(htim, &oc, channel) != HAL_OK)
  {
    return HAL_ERROR;
  }
  /* TIM1 为高级定时器，HAL_TIM_PWM_Start 内部会让能 MOE(主输出) */
  if (HAL_TIM_PWM_Start(htim, channel) != HAL_OK)
  {
    return HAL_ERROR;
  }

  s->attached = 1U;
  Servo_WriteAngle(s, center_angle);

  return HAL_OK;
}

/**
 * @brief  停止某个通道输出（舵机失力）
 */
void Servo_Detach(Servo_t *s)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }
  (void)HAL_TIM_PWM_Stop(s->htim, s->Channel);
  s->attached = 0U;
  s->mode     = SERVO_MODE_IDLE;
}

/**
 * @brief  修改脉宽映射（换不同型号舵机、做行程校准）
 */
void Servo_SetPulseRange(Servo_t *s, float pulse_min_us, float pulse_max_us)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }
  if ((pulse_min_us > 0.0f) && (pulse_max_us > pulse_min_us))
  {
    s->pulse_min_us = pulse_min_us;
    s->pulse_max_us = pulse_max_us;
    Servo_WriteAngle(s, s->angle);
  }
}

/**
 * @brief  修改机械行程范围（如 270° 舵机、限制在安全区间）
 */
void Servo_SetAngleRange(Servo_t *s, float angle_min, float angle_max)
{
  if ((s == 0) || (s->attached == 0U) || (angle_max <= angle_min))
  {
    return;
  }
  s->angle_min = angle_min;
  s->angle_max = angle_max;

  s->angle  = Servo_Clamp(s->angle,  s->angle_min,  s->angle_max);
  s->target = Servo_Clamp(s->target, s->angle_min,  s->angle_max);
  Servo_WriteAngle(s, s->angle);
}

/**
 * @brief  方向反转（反装时用）
 */
void Servo_SetInverted(Servo_t *s, uint8_t inverted)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }
  s->inverted = (inverted != 0U) ? 1U : 0U;
  Servo_WriteAngle(s, s->angle);
}

/**
 * @brief  限速（°/s），0 表示不限速
 */
void Servo_SetSpeedLimit(Servo_t *s, float speed_dps)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }
  s->speed_dps = (speed_dps > 0.0f) ? speed_dps : 0.0f;
}

/**
 * @brief  立即到位
 */
void Servo_SetAngle(Servo_t *s, float angle)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }
  s->angle  = Servo_Clamp(angle, s->angle_min, s->angle_max);
  s->target = s->angle;
  s->mode   = SERVO_MODE_IDLE;
  Servo_WriteAngle(s, s->angle);
}

/**
 * @brief  设定目标角，按 speed_dps 限速运动
 */
void Servo_MoveTo(Servo_t *s, float angle)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }

  s->target        = Servo_Clamp(angle, s->angle_min, s->angle_max);
  s->start_angle   = s->angle;
  s->move_start_ms = HAL_GetTick();
  s->last_tick_ms  = s->move_start_ms;   /* 免得第一帧 dt 过大直接跳到位 */

  if (s->speed_dps <= 0.0f)
  {
    s->angle = s->target;              /* 不限速 = 立刻到位 */
    s->mode  = SERVO_MODE_IDLE;
    Servo_WriteAngle(s, s->angle);
  }
  else
  {
    s->mode = SERVO_MODE_SPEED;
  }
}

/**
 * @brief  在指定时间内柔和到位（S 曲线）
 */
void Servo_MoveIn(Servo_t *s, float angle, uint32_t time_ms)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }

  s->target        = Servo_Clamp(angle, s->angle_min, s->angle_max);
  s->start_angle   = s->angle;
  s->move_start_ms = HAL_GetTick();
  s->last_tick_ms  = s->move_start_ms;

  if (time_ms < 2U)
  {
    s->angle = s->target;
    s->mode  = SERVO_MODE_IDLE;
    Servo_WriteAngle(s, s->angle);
  }
  else
  {
    s->move_time_ms = time_ms;
    s->mode         = SERVO_MODE_TIME;
  }
}

/**
 * @brief  运动状态机更新（主循环 1ms 调一次）
 */
void Servo_Update(Servo_t *s)
{
  uint32_t now;
  uint32_t dt_ms;

  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }
  if (s->mode == SERVO_MODE_IDLE)
  {
    return;                            /* 静止时不写寄存器 */
  }

  now   = HAL_GetTick();
  dt_ms = now - s->last_tick_ms;
  if (dt_ms == 0U)
  {
    return;                            /* 还没过一个 tick */
  }
  s->last_tick_ms = now;

  if (s->mode == SERVO_MODE_SPEED)
  {
    float delta = s->target - s->angle;
    float step  = s->speed_dps * ((float)dt_ms / 1000.0f);

    if (step <= 0.0f)
    {
      s->angle = s->target;
      s->mode  = SERVO_MODE_IDLE;
    }
    else if (delta > step)
    {
      s->angle += step;
    }
    else if (delta < -step)
    {
      s->angle -= step;
    }
    else
    {
      s->angle = s->target;            /* 剩余不足一步，直接对齐 */
      s->mode  = SERVO_MODE_IDLE;
    }
    Servo_WriteAngle(s, s->angle);
  }
  else  /* SERVO_MODE_TIME */
  {
    uint32_t elapsed;

    if (s->move_time_ms == 0U)
    {
      s->angle = s->target;
      s->mode  = SERVO_MODE_IDLE;
    }
    else
    {
      elapsed = now - s->move_start_ms;
      if (elapsed >= s->move_time_ms)
      {
        s->angle = s->target;
        s->mode  = SERVO_MODE_IDLE;
      }
      else
      {
        float t = (float)elapsed / (float)s->move_time_ms;   /* 0~1 */
        float k = t * t * (3.0f - 2.0f * t);                 /* smoothstep */
        s->angle = s->start_angle + ((s->target - s->start_angle) * k);
      }
    }
    Servo_WriteAngle(s, s->angle);
  }
}

/**
 * @brief  批量更新（配结构体数组）
 */
void Servo_UpdateAll(Servo_t *group, uint16_t count)
{
  uint16_t i;

  if (group == 0)
  {
    return;
  }
  for (i = 0U; i < count; i++)
  {
    Servo_Update(&group[i]);
  }
}

/**
 * @brief  读当前角度
 */
float Servo_GetAngle(const Servo_t *s)
{
  return (s != 0) ? s->angle : 0.0f;
}

/**
 * @brief  是否还在运动
 */
uint8_t Servo_IsBusy(const Servo_t *s)
{
  if (s == 0)
  {
    return 0U;
  }
  return (s->mode != SERVO_MODE_IDLE) ? 1U : 0U;
}

/**
 * @brief  调试：直接写脉宽（不改内部角度状态）
 */
void Servo_SetPulseUs(Servo_t *s, float pulse_us)
{
  if ((s == 0) || (s->attached == 0U))
  {
    return;
  }
  Servo_WritePulse(s, Servo_Clamp(pulse_us, 50.0f,
                                  (s->htim->Init.Period + 1U) * s->us_per_tick));
}

/**
 * @brief  调试：读当前实际脉宽
 */
float Servo_GetPulseUs(const Servo_t *s)
{
  if (s == 0)
  {
    return 0.0f;
  }
  return Servo_AngleToPulse(s, s->angle);
}
