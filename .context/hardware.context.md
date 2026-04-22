# 硬件配置

> 数据来源：网表确认（2026-03-25）+ CubeMX `upboard.ioc` + `Core/Src/gpio.c` 当前配置
> 修改前必须对照原理图确认。

## 芯片信息

| 字段 | 值 |
|------|-----|
| 型号 | STM32F407VGT6（LQFP-100） |
| 主频 | 168 MHz |
| Flash | 1024 KB（按 OTA 布局拆 BL / Slot A / Slot B） |
| RAM | 192 KB（128 KB SRAM1 + 64 KB CCM） |

---

## 引脚映射

### 电源/使能
| 信号 | 引脚 | 类型 | 备注 |
|------|------|------|------|
| EN_DVCC_5TO3V3 | PD11 | OUTPUT_PP | 上电即 HIGH（运放解锁，硬件强制） |
| EN_AVCC_5TO3V3 | PD12 | OUTPUT_PP | 上电即 HIGH（运放解锁，硬件强制） |
| EN_TPS43060 | PB13 | OUTPUT_PP | 12V→24V 升压使能（驱动板 U1） |
| EN_24TO12 | PB14 | OUTPUT_PP | 24V→12V 降压使能（驱动板 U5，12V_VCC_FAN） |
| CHARGE_EN | PB12 | OUTPUT_PP | 锂电池充电模块使能 |
| PUMP_EN | PB15 | OUTPUT_PP | 水泵输出使能（FPC2.11 直通） |

上电时序：`PB13→500ms→PB14→200ms→PB12→50ms→PB15`

### 风扇（fan_ctrl）
| 信号 | 引脚 | 类型 | 备注 |
|------|------|------|------|
| FAN_VCC_CTRL | PC6 | OUTPUT_PP | 12V 风扇电源开关（MOSFET Q10） |
| FAN_PWM_CTRL | PC8 | TIM3_CH3 AF2 | 20kHz 调速 |
| FAN_FB | PC7 | TIM8_CH2 AF3 | FG 反馈，开漏，内部上拉 ~40kΩ（建议外接 10kΩ） |

RPM = freq × 20（3 极对）

### 压缩机 YSJ（compressor_ctrl）
| 信号 | 引脚 | 类型 | 备注 |
|------|------|------|------|
| YSJ_PWM | PB5 | TIM3_CH2 AF2 | 20kHz，PMOS 反相（`TIM_OCPOLARITY_LOW`） |
| DIR_CTRL | PB3 | OUTPUT_PP | 0=正转 1=反转 |
| BREAK_CTRL | PD7 | OUTPUT_PP | 1=释放刹车 0=刹死 |
| SC_COUNT | PB4 | TIM3_CH1 AF2 | 转速脉冲输入 |

RPM = freq × 10（6 极对）

### NTC 采集（ADC1 + DMA2 Stream0 循环）
| 引脚 | ADC 通道 | adc_buf | 信号 |
|------|----------|---------|------|
| PA0 | IN0 | [0] | 2-NTC2_SG |
| PA1 | IN1 | [1] | 2-NTC3_SG |
| PA2 | IN2 | [2] | 2-NTC4_SG |
| PA3 | IN3 | [3] | 2-NTC1_SG |
| PA4 | IN4 | [4] | 1-NTC2_SG |
| PA5 | IN5 | [5] | 1-NTC3_SG |
| PA6 | IN6 | [6] | 1-NTC4_SG（早期 gpio.c 误配 OUTPUT_PP，已修复为 ANALOG） |
| PA7 | IN7 | [7] | 1-NTC1_SG（同上） |

12 bit 分辨率，480 周期采样，软件触发连续转换。

### INA226 功率监测
| I2C | SCL/SDA | HAL 地址 | 测量对象 | Rshunt | Current_LSB | cal_val |
|-----|---------|----------|----------|--------|-------------|---------|
| I2C1 | PB6 / PB7 | 0x88 | U13 水泵（24V_YSJ_SENSOR） | 100 mΩ | 0.1 mA | 0x0200 |
| I2C2 | PB10 / PB11 | 0x88 | U11 24V 输入（12V_VCC_UIP） | 6 mΩ | 1.2 mA | 0x0355 |
| I2C3 | PA8 / PC9 | 0x88 | U12 总输入（24V_BAT_UIP） | 6 mΩ | 1.2 mA | 0x0355 |

Config 0x4527 = 16 次平均 + 1.1 ms 转换 + 连续测量。

### 串口
| 信号 | 引脚 | 备注 |
|------|------|------|
| USART2_TX | PD5 | AF7，115200 8N1 |
| USART2_RX | PD6 | AF7 |

⚠ USART2 归 ESP32 使用（auto-memory feedback）。Bootloader / 工具代码禁止打明文，状态用 LED 指示。BLE OTA 通路：网页 → Web Bluetooth → ESP32-C3 BLE↔UART0 透传 → STM32 USART2。

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
| USART2 | 异步 | 115200 8N1 | — | — | ESP32 透传通道（不打明文） |
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
