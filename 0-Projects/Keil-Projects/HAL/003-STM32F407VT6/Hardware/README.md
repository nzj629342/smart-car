# Hardware 封装注意事项（8 路舵机 · HAL 分层版）

> 适用范围：`003-STM32F407VT6`（STM32F407VET6 + HAL 库 + Keil MDK），VSCode + Keil Assistant 编辑。
> 驱动固定占用 3 个定时器共 **8 个 PWM 通道**：TIM1 ×4、TIM9 ×2、TIM12 ×2。
> 本文件只讲“怎么用不出事”，实现细节看源码注释。

---

## 1. 文件夹结构与分工（HAL 两层）

| 文件 | 层次 | 职责 |
|---|---|---|
| `servo_tim.c/.h` | 硬件层（HAL 时基） | `MX_TIM1_Init / MX_TIM9_Init / MX_TIM12_Init` 填 `TIM_HandleTypeDef` 并调 `HAL_TIM_PWM_Init()`；`HAL_TIM_PWM_MspInit()` 里开 TIM/GPIO 时钟、配 8 个复用引脚 |
| `servo.c/.h` | 业务层 | 角度↔脉宽↔CCR 映射、倒装、限速、定时 S 曲线；只用 `HAL_TIM_PWM_ConfigChannel` / `HAL_TIM_PWM_Start` / `__HAL_TIM_SET_COMPARE` / `HAL_TIM_PWM_Stop` |
| `README.md` | 文档 | 本文件 |

这样分层的好处：**换引脚只改 `servo_tim.c` 的 MspInit，换舵机型号只改 `Servo_Attach` 的四个参数**，两边互不影响。

`servo_tim.c` 里定义了 `htim1 / htim9 / htim12` 三个句柄，`servo_tim.h` 用 `extern` 暴露给 `main.c`，所以 main 里直接写 `&htim1` 即可，不用自己定义句柄。

---

## 2. 接入工程的三个前提（缺一个就编不过 / 没波形）

1. `Core/Inc/stm32f4xx_hal_conf.h` 里 `#define HAL_TIM_MODULE_ENABLED` 必须打开（默认注释掉）。
2. Keil 的 `Drivers/STM32F4xx_HAL_Driver` 组里要有 `stm32f4xx_hal_tim.c`、`stm32f4xx_hal_tim_ex.c`。
3. Keil 的 Include Paths 要有 `../Hardware`；VSCode 侧见 `.vscode/c_cpp_properties.json`。

> 本工程已全部配好，直接编译即可（当前 0 Error / 0 Warning）。

---

## 3. 8 路通道与引脚（固定在 `HAL_TIM_PWM_MspInit` 里）

| 序号 | 定时器 | 通道 | 引脚 | 复用(AF) | 备注 |
|---|---|---|---|---|---|
| 0 | TIM1 | CH1 | PE9  | AF1 | 高级定时器 |
| 1 | TIM1 | CH2 | PE11 | AF1 | 高级定时器 |
| 2 | TIM1 | CH3 | PE13 | AF1 | 高级定时器 |
| 3 | TIM1 | CH4 | PE14 | AF1 | 高级定时器 |
| 4 | TIM9 | CH1 | PE5  | AF3 | **只有 2 个通道** |
| 5 | TIM9 | CH2 | PE6  | AF3 | **只有 2 个通道** |
| 6 | TIM12 | CH1 | PB14 | AF9 | **只有 2 个通道** |
| 7 | TIM12 | CH2 | PB15 | AF9 | **只有 2 个通道** |

必须知道的几点：

- **TIM9 / TIM12 只有 2 通道**：`Servo_Attach` 绑 CH3/CH4 会直接返回 `HAL_ERROR`（驱动内部 `Servo_IsChannelValid` 会拦）。别往这两路加第 3 个舵机。
- **TIM1 是高级定时器**：输出需要 MOE（主输出使能）。`HAL_TIM_PWM_Start()` 内部已自动置 MOE，不用手动 `__HAL_TIM_MOE_ENABLE()`。
- **时钟归属**：TIM1、TIM9 在 APB2；TIM12 在 APB1。`Servo_TIM_GetClockHz()` 按 APB 预分频判断（≠1 时定时器时钟 = PCLK×2），当前 HSI 16MHz → PSC=16，计数 1MHz。以后把主频改到 168MHz 不用改代码。
- 三个定时器都是 16 位，50Hz 下 ARR=19999，占空比分辨率足够。
- **引脚冲突自查**：PE2–PE6 常被 FSMC/LCD、SDIO 占；PB14/PB15 可能是 USB_OTG_FS（DM/DP）；PE9–PE14 也可能接外部 SRAM/液晶。若板子上这些脚另有用途，改 `servo_tim.c` 里对应分支的 `gpio.Pin / gpio.Alternate` 和 `__HAL_RCC_GPIOx_CLK_ENABLE`。

---

## 4. 供电与接线（8 路尤其要注意）

- **绝对不要从开发板 5V 排针带 8 个舵机**。SG90 空载约 100mA、堵转 600–700mA，8 个同时启动的浪涌可达 3–5A。
- 用 **5V / 5A 以上**独立电源（或专用舵机驱动板 / 大容量电池），**必须与开发板共地**。
- 每个舵机电源脚就近加 100nF 陶瓷电容；总电源入口加 **≥1000uF** 电解电容吸收浪涌。
- 长信号线（>30cm）建议串 100–220Ω，并在靠舵机端加 10k 下拉。
- 上电顺序：先给 MCU 上电（此时已输出中位），再给舵机电源上电，避免集体乱抖。
- **不要 8 路同时满速启动**：演示代码里各路的往返周期本身是错开的，实际项目里也建议分时启动（每路错开 100–200ms）。

---

## 5. 参数怎么填（`Servo_Attach` 的 4 个浮点）

```c
Servo_Attach(&servo[0], &htim1, TIM_CHANNEL_1, 500.0f, 2500.0f, 0.0f, 180.0f);
/*                    定时器  通道          脉宽下限  脉宽上限  角度下限 角度上限 */
```

- `pulse_min_us / pulse_max_us`：按舵机铭牌填，常见 500/2500（0–180°、0–270°）、1000/2000（部分数码舵机）。
- `angle_min / angle_max`：填**机械行程**，不是舵机极限。机构有限位就收窄（如 20/160），顶死会持续大电流、发热、烧齿轮。
- 映射：角度线性映射到脉宽；`Servo_SetInverted(s,1)` 后镜像（180° → 最低脉宽）。
- 校准：先 `Servo_SetAngle(0)` / `Servo_SetAngle(180)` 看实际位置，差几度就微调 `pulse_*_us`，每次 ±20us。
- 本工程 4/5 号按 270° 舵机配（0–270° ↔ 0.5–2.5ms），6 号额外用 `Servo_SetAngleRange(20,160)` 收紧行程。
- `Attach` 成功后舵机**停在行程中位**，不会上电撞限位。

---

## 6. 三种运动方式

| 接口 | 行为 | 典型用途 |
|---|---|---|
| `Servo_SetAngle()` | 立即写 CCR，一帧到位 | 上电初始化、急停 |
| `Servo_MoveTo()` | 按 `speed_dps` 匀速逼近，可中途改目标 | 限速防甩头、防堵转 |
| `Servo_MoveIn()` | 指定时长 + S 曲线（起停平缓） | 云台、机械臂等柔性动作 |

- `Servo_SetSpeedLimit(s, 0)` = 不限速，此时 `MoveTo` 等价 `SetAngle`。
- `MoveIn(…, 0/1ms)` 退化成立即到位。
- 三种方式都受 `angle_min/angle_max` 限幅，越界目标自动裁到边界。
- 8 路同时用 `MoveIn` 不会互相拖累：每路独立记录 `start_angle / move_start_ms`，靠 `UpdateAll` 统一推进。
- `Servo_Detach()` 只是 `HAL_TIM_PWM_Stop` 停该通道（舵机失力），不是断电。

---

## 7. 主循环必须遵守的两条

1. **`Servo_UpdateAll(servo, SERVO_COUNT)` 每 1ms 调一次**（非阻塞状态机）。不调 → 舵机不动；调得太稀 → 动作一顿一顿。
2. **循环里别放大延时**。等动作完成用：
   ```c
   while (Servo_IsBusy(&servo[0])) { Servo_UpdateAll(servo, SERVO_COUNT); HAL_Delay(1); }
   ```
3. 中断里不要直接改 `Servo_t`：ISR 与主循环会争用同一结构体（可能出现“角度改了、模式没改”的半更新），浮点运算 + 写 CCR 也会拉长中断。中断里只置标志位，回主循环再发指令。（`HAL_GetTick()` 在 ISR 里读取本身安全。）

---

## 8. 常见故障对照表

| 现象 | 先查什么 |
|---|---|
| 编译报 `Servo_TIM_Init` / 引脚表未定义 | 老版接口已移除，初始化改为 `MX_TIMx_Init()`，见第 1 节 |
| 某一组完全不转 | 对应 `MX_TIMx_Init()` 是否调过且返回 `HAL_OK`；`HAL_TIM_MODULE_ENABLED`；`hal_tim.c` 是否在工程 |
| TIM9/TIM12 上第 3 路没反应 | 这两个定时器只有 CH1/CH2，`Attach` 会返回 `HAL_ERROR` |
| TIM1 四路都没波形 | 高级定时器的 MOE：确认走的是 `HAL_TIM_PWM_Start`，没手动清 MOE |
| 全部抖动/抽搐 | 电源电流不够、未共地、电容不足；先单独供电试一路 |
| 一开多路就复位 | 浪涌，加 1000uF + 分时启动；检查 5V 走线 |
| 某个舵机方向相反 | 用 `Servo_SetInverted`，别改接线 |
| 只动一下就停 | 主循环没持续调 `UpdateAll` |
| 角度到位差几度 | 脉宽映射不准，微调 `pulse_*_us` 或收窄 `angle_min/max` |
| 舵机顶死发热 | 行程填太大，机构限位被顶住，收紧 `angle_max` |
| 抖动但电源正常 | 同一通道被 `Attach` 两次覆盖，或别处把该定时器通道改成别的模式 |

---

## 9. 改代码时的边界

- **先 `MX_TIMx_Init()`，再 `Servo_Attach()`**。顺序反了，`Attach` 发现 `htim->Instance == 0` 直接返回 `HAL_ERROR`。
- **每个定时器只 `MX_TIMx_Init()` 一次**；重复调用会重配计数器，正在输出的通道会闪一下。
- 一个 `Servo_t` 只绑一个通道；同一通道不要 `Attach` 两次（第二次会覆盖 OC 配置，脉宽会跳）。
- `Servo_SetPulseUs()` 是调试口，不更新内部角度状态，调完记得 `Servo_SetAngle()` 归位。
- 想加第 9 路：在 `servo_tim.c` 加一个 `MX_TIMx_Init`（或复用已有定时器空通道）+ 在 `HAL_TIM_PWM_MspInit` 补该实例的时钟和引脚 + 在 `servo.h` 改 `SERVO_CHANNEL_COUNT`。

---

## 10. 与 CubeMX 共存时的坑

- 本项目 `.ioc` 里**没有**配 TIM，所以 `servo_tim.c` 是手写的。若以后在 CubeMX 里启用 TIM1/TIM9/TIM12 并生成代码，会产出 `Core/Src/tim.c` 和 `Core/Src/stm32f4xx_hal_msp.c` 里的 `HAL_TIM_PWM_MspInit`，与 `servo_tim.c` 里的**同名强定义冲突（链接报重复定义）**——二选一：要么删掉 `servo_tim.c` 改用 CubeMX 生成的，要么把 CubeMX 里 TIM 全部关掉。
- CubeMX 重新生成后要复查：`HAL_TIM_MODULE_ENABLED` 是否被注释回去、uvprojx 里 `Hardware` 组 / `../Hardware` 路径 / `hal_tim*.c` 是否还在、`main.c` 的 `USER CODE` 区会保留而外设代码会被覆盖。`Hardware/` 目录本身不会被删。

---

## 11. 工程与工具链

- Keil 的 `Hardware` 组已登记 `servo.c`、`servo_tim.c`、`servo.h`、`servo_tim.h`、`README.md`，VSCode 的 **KEIL PROJECT** 树能看到 `.h` 和 `.md`；改完工程若看不到，执行 `Refresh Keil Project` 或 `Developer: Reload Window`。
- `.vscode/c_cpp_properties.json` 已配好包含路径与 `USE_HAL_DRIVER / STM32F407xx` 宏，`.h` 打开不报红。
- 编译命令（PowerShell，必须等待进程结束才知道结果）：
  ```powershell
  Start-Process -FilePath 'D:\Keil5\UV4\UV4.exe' `
    -ArgumentList @('-r','"D:\git-repositories\0-Projects\Keil-Projects\HAL\003-STM32F407VT6\MDK-ARM\003-SPL-STM32F407VT6.uvprojx"','-o','"build.log"','-j0') `
    -Wait -PassThru -NoNewWindow
  ```

---

## 12. 快速接线核对表（贴板子前逐条打勾）

- [ ] 8 路信号分别接 PE9 / PE11 / PE13 / PE14 / PE5 / PE6 / PB14 / PB15
- [ ] 舵机电源接独立 5V，**GND 与开发板 GND 相连**
- [ ] 电源入口有 ≥1000uF 电解电容，各舵机就近有 100nF
- [ ] 这些引脚在原理图上没有接 LCD/FSMC/SDIO/USB 等其它功能
- [ ] 先只接 1 路验证 20ms 周期、0.5–2.5ms 脉宽，再接全部 8 路
- [ ] 上电观察是否回中位（不撞限位），再跑 main.c 的演示
