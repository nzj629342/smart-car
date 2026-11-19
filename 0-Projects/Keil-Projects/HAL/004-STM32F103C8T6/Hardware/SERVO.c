#include "stm32f10x.h"                  // Device header


/**配置引脚时钟和模式，初始化
  *
  *
  */
  
#include "servo.h"

// 定时器2 CH1 PA0  PWM输出
// 系统时钟CLK_PSC：72MHz
// 预分频PSC=72-1，自动重装载ARR=20000-1
// PWM频率Freq=CLK_PSC/(PSC+1)(ARR+1)=50Hz
// PWM占空比Duty=CCR/(ARR+1)
// PWM分辨率Reso=1/(ARR+1)
// 普通舵机要求：Freq=50Hz   正常工作0.5ms=-90 ->CCR=500  2.5ms=90 ->CCR=2500(us)

// 初始化函数
void Servo_Init(void)
{	
	// 结构体命名
    GPIO_InitTypeDef GPIO_InitStruct;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStruct;
    TIM_OCInitTypeDef TIM_OCInitStruct;
	
	// 开启RCC时钟,使能
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
	
	// TIM使用内部时钟
	TIM_InternalClockConfig(TIM2);

    // PA0 复用推挽输出
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 时基配置
    TIM_TimeBaseStruct.TIM_Prescaler = 72 - 1;	//PSC分频器
    TIM_TimeBaseStruct.TIM_Period = 20000 - 1;	//ARR周期
    TIM_TimeBaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStruct);

	// TIM初始化
	// TIM_OCStructInit(&GPIO_InitStruct);
	
    // TIM选用PWM模式1
    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStruct.TIM_Pulse = 1500;	//CCR预设值(500-2500)
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
	
	// 启用TIM2的CH1通道
    TIM_OC1Init(TIM2, &TIM_OCInitStruct);
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
	
	// 启用TIM2的CH2通道
    TIM_OC2Init(TIM2, &TIM_OCInitStruct);
    TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);
	
	// 使能
    TIM_Cmd(TIM2, ENABLE);
}

/**
 * @brief 设置舵机角度 0~180
 */
void Servo0_SetAngle(uint16_t angle)
{
    uint16_t pulse;
    if(angle > SERVO_MAX_ANGLE) angle = SERVO_MAX_ANGLE;

    pulse = SERVO_MIN_PULSE + (SERVO_MAX_PULSE - SERVO_MIN_PULSE) * angle / SERVO_MAX_ANGLE;
    TIM_SetCompare1(TIM2, pulse);
}

void Servo1_SetAngle(uint16_t angle)
{
    uint16_t pulse;
    if(angle > SERVO_MAX_ANGLE) angle = SERVO_MAX_ANGLE;

    pulse = SERVO_MIN_PULSE + (SERVO_MAX_PULSE - SERVO_MIN_PULSE) * angle / SERVO_MAX_ANGLE;
    TIM_SetCompare2(TIM2, pulse);
}


// -------------------- 非阻塞变量 --------------------
typedef struct
{
    uint16_t curAng;
    uint16_t tarAng;
    uint16_t stepMs;
    uint32_t tickLast;
}ServoSmooth_t;

static ServoSmooth_t s0 = {90,90,10,0};
static ServoSmooth_t s1 = {90,90,10,0};

void Servo0_SmoothStart(uint16_t targetAngle, uint16_t stepMs)
{
    if(targetAngle > SERVO_MAX_ANGLE) targetAngle = SERVO_MAX_ANGLE;
    s0.tarAng = targetAngle;
    s0.stepMs = stepMs;
}

void Servo1_SmoothStart(uint16_t targetAngle, uint16_t stepMs)
{
    if(targetAngle > SERVO_MAX_ANGLE) targetAngle = SERVO_MAX_ANGLE;
    s1.tarAng = targetAngle;
    s1.stepMs = stepMs;
}

// 放在while(1)循环里不断调用，不要放延时！
void Servo_SmoothTask(void)
{
    uint32_t now = SysTick->VAL; // 简易计时，如果你有ms定时器可以换成系统ms tick

    // Servo0
    if(s0.curAng != s0.tarAng)
    {
        if((now - s0.tickLast) >= s0.stepMs)
        {
            if(s0.curAng < s0.tarAng)
                s0.curAng++;
            else
                s0.curAng--;
            Servo0_SetAngle(s0.curAng);
            s0.tickLast = now;
        }
    }
    // Servo1
    if(s1.curAng != s1.tarAng)
    {
        if((now - s1.tickLast) >= s1.stepMs)
        {
            if(s1.curAng < s1.tarAng)
                s1.curAng++;
            else
                s1.curAng--;
            Servo1_SetAngle(s1.curAng);
            s1.tickLast = now;
        }
    }
}
