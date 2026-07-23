# 硬件配置

> 数据来源 (按权威度排序):
>   1. 原理图 `SCH_Schematic6_2026-04-21` (IO 未分配 page) — 最终事实
>   2. `Core/Src/gpio.c` + `Core/Src/usart.c` + `Core/Src/stm32f4xx_hal_msp.c` — 已对齐 V6
>   3. `app/Src/power_ctrl.c` + `app/Src/fan_ctrl.c` 等模块注释 — 已对齐 V6
> 修改前必须对照原理图确认。**V3 旧引脚映射已全部移植,不要相信任何 PB5/PB3/PB9/PC6/PC7/PC8/PB13/PB14/PB15 跟风扇/水泵/压缩机相关的旧描述**。

## V7 压缩机接口变更（2026-07-23 用户确认，修订原理图待入库）

| 功能 | MCU 引脚 | 模式 | 当前确认状态 |
|------|----------|------|--------------|
| 压缩机速度 PWM | PA15 | TIM2_CH1 / AF1 | 5kHz PWM1，高电平有效；ARR=199、1MHz timer tick |
| 压缩机正反转 NET14 | PC9 | GPIO output | HIGH=正转、LOW=反转；主控网表确认 U9.66 → R10 0Ω → FPC2.14 |
| 压缩机停转 NET15 | PA8 | GPIO output | HIGH=运行、LOW=停转；主控网表确认 U9.67 → R11 0Ω → FPC2.15 |

**V7 固件处理状态**：
- PA15 已从 `fan_ctrl` 移交 `compressor_ctrl`，配置 TIM2_CH1 5kHz active-HIGH PWM。
- I2C3 初始化、扫描、恢复及 MCF8329A 启动/周期任务已停用，PA8/PC9 固定为推挽 GPIO。
- App 和 bootloader 均先设置 PA8 LOW、PWM=0；App 默认 PC9 LOW 反转，bootloader 停转期间保持 PC9 HIGH。
- App 启动压缩机时先选择 PC9 LOW 反转，再解除 PA8 STOP，并由主循环非阻塞地将 PA15 占空比在 5000ms 内从 0% 线性升至目标值；当前开机和 SET_GEAR ON 目标均为 100%。
- 2026-07-23 已烧录 bootloader + App A；SWD 取样为 109ms/2%、2116ms/42%、5000ms/100%，最终 TIM2 CR1=0x81、ARR=99、CCR1=100，GPIOA ODR bit8=1、GPIOC ODR bit9=1。实际引脚波形仍待示波器确认。
- 2026-07-23 已继续烧录默认反转版 App A；SWD 重新按最新 ELF 取址确认 `g_compressor_reverse=1`、`g_compressor_pwm_duty_pct=100`、`g_compressor_stop_asserted=0`、elapsed=5000ms，GPIOC ODR bit9=0（PC9 LOW 反转）、GPIOA ODR bit8=1（PA8 HIGH 运行）。
- 2026-07-23 用户要求暂时关闭软启动：当前待烧录版本在风扇/水泵先行 500ms 后，压缩机 PC9 LOW 反转并立即输出 PA15 100% PWM；5s 斜坡代码保留但由编译开关关闭。
- 2026-07-23 已烧录该版本 App A；SWD 读到 duty=100%、ramp_active=0、reverse=1、stop=0，TIM2 CR1=0x81、ARR=99、CCR1=100，GPIOA ODR=0x100（PA8 HIGH），GPIOC ODR=0xC00（PC10/PC11 HIGH、PC9 LOW）。
- 2026-07-23 已烧录 5kHz 版本 App A；SWD 读到 TIM2 CR1=0x81、PSC=83、ARR=199、CCR1=200，对应 1MHz/(199+1)=5kHz 和 100%占空比；GPIOA ODR bit8=1、GPIOC ODR bit10=1/bit9=0。
- PA15 不与当前蜂鸣器冲突：蜂鸣器实际使用 PB14 / TIM1_CH2N。
- 风扇原 PWM 通道已被压缩机占用；风扇的新 PWM/使能方案尚未提供。
- 2026-07-23 用户确认风扇后续不再使用 PWM，仅由 PC10 `FAN_VCC` 供电使能控制：HIGH=运行、LOW=停止；开机和 SET_GEAR ON 使能，SET_GEAR OFF 关闭。
- 当前 V6 主控板网表已确认 NET14/NET15 到 PC9/PA8 的排线通路；新版驱动板修订图尚未入库，外部上下拉及 3.3V 输入兼容性仍待图纸确认。

## 芯片信息

| 字段 | 值 |
|------|-----|
| 型号 | STM32F407VET6（LQFP-100） |
| 主频 | 168 MHz |
| Flash | 512 KB（OTA 布局占前 384 KB：BL 32K + 参数区 32K + Slot A/B 各 128K，余 128 KB 备用） |
| RAM | 192 KB（128 KB SRAM1 + 64 KB CCM） |

---

## 引脚映射

### 电源/使能 (V6 实际, 以 `app/Src/power_ctrl.c` 为准)
| 信号 | 引脚 | 极性 | API | 备注 |
|------|------|------|------|------|
| EN_DVCC_5TO3V3 | PD11 | active HIGH | (硬件强制) | 上电即 HIGH（运放解锁） |
| EN_AVCC_5TO3V3 | PD12 | active HIGH | (硬件强制) | 上电即 HIGH（运放解锁） |
| BOOST (12V→24V) | PE6 | active LOW | `PowerCtrl_EnableBoost(en)` | DCDC 升压使能 |
| LOAD (24V out) | PE5 | active HIGH | `PowerCtrl_EnableLoad(en)` | 24V 负载输出使能 |
| CHARGE_NTC | PC15 | active LOW | `PowerCtrl_EnableChargeNtc(en)` | 充电器 NTC 使能 |
| **FAN_VCC** | **PC10** | active HIGH | `PowerCtrl_EnableFanVcc(en)` | 12V 风扇 MOSFET Q10 |
| **PUMP** | **PC11** | active HIGH | `PowerCtrl_EnablePump(en)` | 水泵输出使能 |

启动时序 (`PowerCtrl_StartupSequence`):
`100ms → BOOST=ON → 200ms → LOAD=ON → 100ms → CHARGE_NTC=ON → 50ms` (FAN/PUMP 由业务命令开启)

SWD 现场切换 (GPIOC BSRR @ 0x40020818, 低 16 bit set, 高 16 bit reset):
- `mww 0x40020818 0x00000400` 开 FAN_VCC (PC10 HIGH)
- `mww 0x40020818 0x04000000` 关 FAN_VCC
- `mww 0x40020818 0x00000800` 开 PUMP (PC11 HIGH)
- `mww 0x40020818 0x08000000` 关 PUMP

### 风扇（fan_ctrl）— V6 实际
| 信号 | 引脚 | AF / TIM | 备注 |
|------|------|------|------|
| FAN_VCC_CTRL | **PC10** | OUTPUT_PP (active HIGH) | 12V 风扇供电 MOSFET, `PowerCtrl_EnableFanVcc()` |
| FAN_PWM_CTRL | **PA15** | TIM2_CH1 AF1 | 20kHz 调速,`FanCtrl_SetDuty()` |
| FAN_FB_OUT | **PB4** | TIM3_CH1 AF2 | FG 输入捕获,`FanCtrl_GetRPM()` (= freq × 20) |

RPM = freq × 20（3 极对）

### 压缩机 — V6 实际 (MCF8329A 直接驱动,无外置 PWM/DIR GPIO 输出)
| 信号 | 引脚 | 类型 | 备注 |
|------|------|------|------|
| DRVOFF | PC12 | OUTPUT_PP | 1=断驱动,0=使能 |
| SPEED_WAKE | PD0 | OUTPUT_PP | 1=唤醒 |
| DIR | PD3 | OUTPUT_PP | 默认 0; **可能被 EEPROM PERI_CONFIG1 DIR_INPUT override 屏蔽** |
| BREAK | PD4 | OUTPUT_PP | 1=刹车 |
| nFAULT | PD5 | INPUT (外部 5.1kΩ 上拉) | LOW=故障 |
| FG | PB9 | TIM4_CH4 AF2 | 压缩机转速反馈 (input capture) |
| EXT_CLK | PA7 | TIM14_CH1 AF9 | 当前暂未使用 (R11=0Ω 跳到芯片) |
| SOX | PC1 | ADC1_IN11 | 压缩机驱动通用 ADC,当前未读 |
| I2C3 SCL | PA8 | AF4 | MCF8329A 控制 (I2C3),target addr 因 Motor Studio 配置可变,Init 自动扫描 |
| I2C3 SDA | PC9 | AF4 | 同上 |

⚠ V3 旧 YSJ_PWM/DIR/BREAK 在 PB3/PB5/PD7 — **已废弃**,V6 PB3/PB5 释放, PB4 让给风扇 FG, PD7 不再用.

### NTC 采集 — V6 实际
ADC1 + DMA **当前未初始化** (`HAL_ADC_*` 未在固件中调用)。8 路 NTC 数据走外部综合采样板 → **USART3 (PD8/PD9)** 帧上来,`sensor_acq.c::SensorAcq_OnNtcFrame()` 处理。
PA0-PA7 当前角色:
- PA0 = SYS_WKUP (KEYWAKE 输入)
- PA1 = 未分配
- PA2/PA3 = USART2 (IOT EC801E)
- PA4/PA5/PA6 = 未分配
- PA7 = TIM14_CH1 EXT_CLK (留作 MCF8329A 时钟,目前未启用)

### INA226 功率监测 — V6 实际 (`Core/Src/main.c:251-253`)
| sensors[] | I2C | SCL/SDA | HAL addr | 测量对象 |
|---|---|---|---|---|
| `sensors[0]` | I2C1 | PB6 / PB7 | 0x80 (A1=GND, A0=GND) | 24V_BAT_UIP 锂电池总输入 |
| `sensors[1]` | I2C1 | PB6 / PB7 | 0x8A (A1=VS, A0=VS) | 24V_YSJ_SENSOR 压缩机功率 |
| `sensors[2]` | I2C2 | PB10 / PB11 | 0x80 | 12V_VCC_UIP 电瓶输入 |

cal_val/Current_LSB 见 main.c 初始化,**注意 sensors[0] 和 [1] 共享 I2C1 总线靠 A0/A1 strap 区分地址**。

### 串口 — V6 实际 (`Core/Src/usart.c:6-9`)
| USART | TX/RX 引脚 | AF | 用途 | 模块 |
|---|---|---|---|---|
| USART1 | PA9 / PA10 | AF7 | 有线遥控 (stub) | `RemoteCtrl_*` |
| USART2 | PA2 / PA3 | AF7 | EC801E 物联网 (115200 8N1),经 R15/R18 100Ω；PA3 RX 内部上拉 | `IotCtrl_*` |
| USART3 | PD8 / PD9 | AF7 | 综合采样板 (NTC 数据上行) | `SamplerComm_*` |
| **USART6** | **PC6 / PC7** | AF8 | **ESP32-C3 BLE OTA + Gear/Status**, 经 R6/R8 100Ω | `EspComm_*` |

NVIC 优先级:USART3=1,0; USART6=1,1; USART2=2,0; USART1=2,1.
BLE OTA 通路:Web Bluetooth → ESP32-C3 BLE↔UART → STM32 USART6 (帧协议 `0xAA | CMD | LEN | PAYLOAD | XOR`).

⚠ 旧文档说 ESP32 在 USART2 PD5/PD6 — **V3 残留,已废弃**。

### 其他
| 信号 | 引脚 | 备注 |
|------|------|------|
| BEEP_CTRL | PA15 | TIM2_CH1 AF1，无源蜂鸣器 PWM |
| LED1 | PD13 | 板载 |
| LED2 | PD14 | 板载（Bootloader 槽位指示：1=fallback / 2=A / 3=B） |
| CHARGE_EN | PB12 | 锂电池充电模块使能（早期网表标 NETQ2/TP16，已确认实际为 CHARGE_EN） |
| P14 | PB1 | FPC2.14 直通，驱动板 sensor1 |

---

## 时钟树

| 时钟 | 频率 | 备注 |
|------|------|------|
| HSE | 8 MHz | 外部晶振 |
| SYSCLK | 168 MHz | PLL |
| AHB (HCLK) | 168 MHz | |
| APB1 (PCLK1) | 42 MHz；TIM1×=84 MHz | TIM2/3 计数走 84 MHz |
| APB2 (PCLK2) | 84 MHz；TIM2×=168 MHz | TIM8 计数走 168 MHz |
| ADC | PCLK2 / 4 = 21 MHz | |

---

## 外设配置

| 外设 | 模式 | 关键参数 | DMA | 中断 | 备注 |
|------|------|----------|-----|------|------|
| TIM2_CH1 | PWM 输出 | PSC=83→1MHz 计数，ARR 可变 | — | — | 蜂鸣器音调 |
| TIM3_CH1 | 输入捕获 | PSC=83→1MHz，ARR=49 | — | TIM3_IRQn 1-0 | 压缩机 SC_COUNT |
| TIM3_CH2 | PWM 输出 | 同上，20kHz，极性 LOW | — | 同上 | 压缩机 YSJ_PWM（PMOS 反相） |
| TIM3_CH3 | PWM 输出 | 同上，20kHz | — | 同上 | 风扇 FAN_PWM_CTRL |
| TIM8_CH2 | 输入捕获 | PSC=167→1MHz，ARR=65535 | — | TIM8_UP_TIM13_IRQn + TIM8_CC_IRQn 1-0 | 风扇 FG（双中断向量，见 known-bugs.md） |
| ADC1 | 扫描+连续+DMA 循环 | 12 bit，480 周期，8 通道 | DMA2 Stream0 | — | NTC |
| I2C1/2/3 | Master 标准 | 100 kHz | — | — | INA226，HAL 阻塞读取 |
| USART2 | 异步 | 115200 8N1；PA3 RX pull-up | — | USART2_IRQn 2-0 | EC801E AT 通道 |
| IWDG | — | /256，RLR=500，约 4 s | — | — | Bootloader 启用，App 主循环+长任务内部喂狗 |
| RTC BKP0R | — | 备份寄存器，软复位不清 | — | — | Bootloader→App 槽位标记（0xA0A0A0A0=A，0xB0B0B0B0=B） |

---

## 已知问题（来自 knowledge-base）

| 问题 | 位置 | 文件 |
|------|------|------|
| TIM8 必须分别使能 UP 和 CC 两个 IRQ | 风扇 FG | `known-bugs.md` |
| ST-LINK `-r32 count > 2` 不可靠 | 多变量诊断 | `known-bugs.md` |
| `printf` 在 newlib-nano 默认无输出（GCC）/ MicroLib 未开启（Keil） | 调试串口 | `hal-patterns.md` |
| TIM PWM+IC 混用初始化顺序 | TIM3 | `hal-patterns.md` |
| volatile 多变量需快照 + 关中断 | 频率计算 | `hal-patterns.md` |
| I2C GPIO 模式切换导致永久 BUSY | I2C 调试 | auto-memory feedback |
