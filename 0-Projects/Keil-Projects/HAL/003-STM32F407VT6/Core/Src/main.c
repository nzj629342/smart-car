/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "servo.h"
#include "servo_tim.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 三个定时器句柄定义在 servo_tim.c（HAL 时基层），这里通过 servo_tim.h 的
   extern 声明直接用：htim1 带 4 路，htim9 / htim12 各带 2 路。            */

/* 舵机对象数组：一个元素 = 一个舵机，各自独立的行程/方向/速度/运动方式 */
#define SERVO_COUNT  SERVO_CHANNEL_COUNT      /* 8 */
Servo_t servo[SERVO_COUNT];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* 8 路演示参数：往返周期(ms)、两端角度(°)、定时模式下的单程时间(ms) */
  static const uint32_t s_period[SERVO_COUNT]  = {2000U,  2400U,  2800U,  3200U,
                                                  2000U,  2400U,  2800U,  3200U};
  static const float    s_lo[SERVO_COUNT]      = {   0.0f,   0.0f,   30.0f,   60.0f,
                                                      0.0f,  30.0f,   30.0f,   90.0f};
  static const float    s_hi[SERVO_COUNT]      = { 180.0f, 180.0f,  150.0f,  120.0f,
                                                    270.0f, 240.0f,  150.0f,  180.0f};
  static const uint32_t s_move_ms[SERVO_COUNT] = {  900U,   700U,   500U,   300U,
                                                   1200U,  1000U,   800U,   600U};

  uint32_t last_cmd[SERVO_COUNT] = {0};
  uint8_t  dir[SERVO_COUNT]      = {0};
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */
  /* 1) HAL 时基层：三个定时器各自初始化为 50Hz PWM（1 tick ≈ 1us）
        内部走 HAL_TIM_PWM_Init()，时钟与 PE/PB 复用脚由 HAL_TIM_PWM_MspInit 处理 */
  if (MX_TIM1_Init()  != HAL_OK) { Error_Handler(); }
  if (MX_TIM9_Init()  != HAL_OK) { Error_Handler(); }
  if (MX_TIM12_Init() != HAL_OK) { Error_Handler(); }

  /* 2) 8 路通道依次绑定：引脚已由 HAL_TIM_PWM_MspInit 配好固定对应关系，
        这里只声明“哪个定时器的哪个通道”；传 TIM9/TIM12 的 CH3/CH4 会返回 HAL_ERROR */
  /* 0 号: TIM1_CH1  -> PE9                           */
  if (Servo_Attach(&servo[0], &htim1, TIM_CHANNEL_1, 500.0f, 2500.0f,   0.0f, 180.0f) != HAL_OK) { Error_Handler(); }
  /* 1 号: TIM1_CH2  -> PE11                          */
  if (Servo_Attach(&servo[1], &htim1, TIM_CHANNEL_2, 500.0f, 2500.0f,   0.0f, 180.0f) != HAL_OK) { Error_Handler(); }
  /* 2 号: TIM1_CH3  -> PE13                          */
  if (Servo_Attach(&servo[2], &htim1, TIM_CHANNEL_3, 500.0f, 2500.0f,   0.0f, 180.0f) != HAL_OK) { Error_Handler(); }
  /* 3 号: TIM1_CH4  -> PE14                          */
  if (Servo_Attach(&servo[3], &htim1, TIM_CHANNEL_4, 500.0f, 2500.0f,   0.0f, 180.0f) != HAL_OK) { Error_Handler(); }
  /* 4 号: TIM9_CH1  -> PE5（270° 舵机）             */
  if (Servo_Attach(&servo[4], &htim9, TIM_CHANNEL_1, 500.0f, 2500.0f,   0.0f, 270.0f) != HAL_OK) { Error_Handler(); }
  /* 5 号: TIM9_CH2  -> PE6（270° 舵机）             */
  if (Servo_Attach(&servo[5], &htim9, TIM_CHANNEL_2, 500.0f, 2500.0f,   0.0f, 270.0f) != HAL_OK) { Error_Handler(); }
  /* 6 号: TIM12_CH1 -> PB14                          */
  if (Servo_Attach(&servo[6], &htim12, TIM_CHANNEL_1, 500.0f, 2500.0f,  0.0f, 180.0f) != HAL_OK) { Error_Handler(); }
  /* 7 号: TIM12_CH2 -> PB15                          */
  if (Servo_Attach(&servo[7], &htim12, TIM_CHANNEL_2, 500.0f, 2500.0f,  0.0f, 180.0f) != HAL_OK) { Error_Handler(); }

  /* 3) 每路给不同配置，这样同一个“往返”指令会有不同表现 */
  Servo_SetSpeedLimit(&servo[0], 100.0f);         /* 0 号 100°/s：180° 约 1.8s 到位 */
  Servo_SetInverted(&servo[1], 1U);               /* 1 号反装：角度→脉宽镜像       */
  Servo_SetSpeedLimit(&servo[1], 200.0f);         /* 1 号 200°/s：比 0 号快一倍     */
  Servo_SetAngleRange(&servo[6], 20.0f, 160.0f);  /* 6 号机构有限位，收紧到 20~160° */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    uint16_t i;

    /* 8 路各按自己的周期往返，互不干扰：
       - 0/1 号用“限速模式”：目标角一样，但 1 号反装 → 物理上镜像转动；
       - 2~7 号用“定时模式”（S 曲线）：单程时间从 300ms 到 1200ms 不等。 */
    for (i = 0U; i < SERVO_COUNT; i++)
    {
      if ((now - last_cmd[i]) >= s_period[i])
      {
        last_cmd[i] = now;
        dir[i] = (dir[i] == 0U) ? 1U : 0U;

        if (i < 2U)
        {
          Servo_MoveTo(&servo[i], (dir[i] != 0U) ? s_hi[i] : s_lo[i]);
        }
        else
        {
          Servo_MoveIn(&servo[i], (dir[i] != 0U) ? s_hi[i] : s_lo[i], s_move_ms[i]);
        }
      }
    }

    /* 1ms 刷新一次状态机（限速与 S 曲线的计算基准，缺了它就不动） */
    Servo_UpdateAll(servo, SERVO_COUNT);
    HAL_Delay(1);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
