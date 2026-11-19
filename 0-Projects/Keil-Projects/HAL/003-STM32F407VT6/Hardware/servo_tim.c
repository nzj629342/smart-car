/**
  ******************************************************************************
  * @file    servo_tim.c
  * @brief   8 路舵机 PWM 的 HAL 时基层
  * @note    1) MX_TIMx_Init() 只做“时基参数 + HAL_TIM_PWM_Init()”，
  *             对应 CubeMX 生成的 tim.c 内容；
  *          2) HAL_TIM_PWM_MspInit() 是 HAL 的弱回调，由 HAL_TIM_PWM_Init()
  *             自动调用，在这里开定时器/GPIO 时钟并配复用引脚；
  *          3) 时基统一做成 1MHz 计数、20ms 周期(50Hz)，故 CCR 值 = 脉宽 us。
  ******************************************************************************
  */

#include "servo_tim.h"
#include "servo.h"

/* 定时器句柄 ----------------------------------------------------------------*/
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim9;
TIM_HandleTypeDef htim12;

/* ============================ 内部工具 ============================ */

/**
 * @brief  按 HAL 的要求填 Init 结构并初始化 PWM 时基
 * @note   计数频率 ≈ 1MHz → 1 tick ≈ 1us；周期 = 20ms(50Hz)
 */
static HAL_StatusTypeDef Servo_TIM_PWMInit(TIM_HandleTypeDef *htim, TIM_TypeDef *inst)
{
  uint32_t clk;
  uint32_t psc;

  htim->Instance = inst;

  clk = Servo_TIM_GetClockHz(inst);
  if (clk == 0U)
  {
    return HAL_ERROR;
  }

  psc = (clk + 500000U) / 1000000U;          /* 16MHz -> 16 分频 = 1MHz */
  if (psc < 1U)
  {
    psc = 1U;
  }
  if (psc > 65536U)
  {
    return HAL_ERROR;
  }

  htim->Init.Prescaler         = psc - 1U;
  htim->Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim->Init.Period            = (1000000U / SERVO_PWM_FREQ_HZ) - 1U;   /* 19999 */
  htim->Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim->Init.RepetitionCounter = 0U;
  htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

  return HAL_TIM_PWM_Init(htim);
}

/* ============================ 对外接口 ============================ */

/**
 * @brief  TIM1 时基初始化（4 路：PE9/PE11/PE13/PE14，AF1）
 */
HAL_StatusTypeDef MX_TIM1_Init(void)
{
  return Servo_TIM_PWMInit(&htim1, TIM1);
}

/**
 * @brief  TIM9 时基初始化（2 路：PE5/PE6，AF3）
 */
HAL_StatusTypeDef MX_TIM9_Init(void)
{
  return Servo_TIM_PWMInit(&htim9, TIM9);
}

/**
 * @brief  TIM12 时基初始化（2 路：PB14/PB15，AF9）
 */
HAL_StatusTypeDef MX_TIM12_Init(void)
{
  return Servo_TIM_PWMInit(&htim12, TIM12);
}

/* ============================ HAL 弱回调 ============================ */

/**
 * @brief  PWM 外设底层初始化：开时钟 + 配复用推挽引脚
 * @note   由 HAL_TIM_PWM_Init() 内部自动调用（stm32f4xx_hal_tim.c 里的 __weak 函数）
 */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
  GPIO_InitTypeDef gpio = {0};

  if (htim->Instance == TIM1)
  {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE, &gpio);
  }
  else if (htim->Instance == TIM9)
  {
    __HAL_RCC_TIM9_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF3_TIM9;
    HAL_GPIO_Init(GPIOE, &gpio);
  }
  else if (htim->Instance == TIM12)
  {
    __HAL_RCC_TIM12_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_14 | GPIO_PIN_15;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF9_TIM12;
    HAL_GPIO_Init(GPIOB, &gpio);
  }
  else
  {
    /* 其它定时器不归本驱动管 */
  }
}

/**
 * @brief  PWM 外设底层反初始化
 */
void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1)
  {
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14);
    __HAL_RCC_TIM1_CLK_DISABLE();
  }
  else if (htim->Instance == TIM9)
  {
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_5 | GPIO_PIN_6);
    __HAL_RCC_TIM9_CLK_DISABLE();
  }
  else if (htim->Instance == TIM12)
  {
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_14 | GPIO_PIN_15);
    __HAL_RCC_TIM12_CLK_DISABLE();
  }
  else
  {
    /* nothing */
  }
}
