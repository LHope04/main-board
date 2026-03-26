# 摩托车冷却控制板固件开发文档

**项目**：摩托车液冷系统主控板（upboard）
**MCU**：STM32F407VGT6（LQFP-100，168MHz）
**工具链**：Keil MDK 5 / STM32 HAL
**文档版本**：v1.0
**日期**：2026-03-26

---

## 一、产品概述

本板负责摩托车液冷系统的核心控制，功能包括：

| 功能模块 | 描述 | 当前状态 |
|----------|------|----------|
| 受控上电 | 按顺序使能各路电源，防止浪涌 | ✅ 已验收 |
| 风扇控制 | PWM 调速 + FG 转速反馈 | ✅ 已验收 |
| 压缩机控制 | PWM 调速 + 换向 + 刹车 + 转速反馈 | ✅ 已验收 |
| 温度采集 | 8 路 NTC 热敏电阻采样 | ✅ 已验收 |
| 功率监测 | 3 路 INA226 电压/电流/功率 | ✅ 已验收 |
| 开机提示 | 大疆风格开机音 + LED 指示 | ✅ 已验收 |
| 充电控制 | 锂电池充电模块使能 | ✅ 已接入 |
| 水泵控制 | 水泵输出使能 | ✅ 已接入 |
| 保护逻辑 | 过温/过流保护状态机 | 🔲 待开发 |

**串口监控输出**（USART2，115200bps，每 500ms 一次）：

```
v1 = 12.450V, i1 = 1.230A, p1 = 1.528W
v2 = 24.100V, i2 = 2.100A, p2 = 50.610W
v3 = 23.950V, i3 = 0.980A, p3 = 23.471W
[FAN] 45 Hz  900 RPM  [YSJ] 720 Hz  7200 RPM
[NTC] 2-1= 2048 2-2= 2051 2-3= 2060 2-4= 2044
[NTC] 1-1= 2033 1-2= 2039 1-3= 2047 1-4= 2041
```

---

## 二、硬件接口映射

### 2.1 电源控制（GPIOB，OUTPUT_PP，初始 LOW）

| 引脚 | 网络名 | 功能 | 使能电平 | 驱动对象 |
|------|--------|------|----------|----------|
| PB12 | CHARGE_EN | 锂电池充电模块使能 | HIGH | 充电 IC |
| PB13 | EN_TPS43060 | 12V→24V 升压使能 | HIGH | TPS43060（U1） |
| PB14 | EN_24TO12 | 24V→12V 降压使能 | HIGH | 降压芯片（U5） |
| PB15 | PUMP_EN | 水泵输出使能 | HIGH | 水泵电路 |

**上电时序（顺序不可更改）：**

```
PB13 HIGH → 等待 500ms → PB14 HIGH → 等待 200ms → PB12 HIGH → PB15 HIGH
```

### 2.2 运放解锁（GPIOD，上电即 HIGH）

| 引脚 | 功能 | 初始电平 |
|------|------|----------|
| PD11 | 运放电路解锁 A | HIGH（硬件强制要求） |
| PD12 | 运放电路解锁 B | HIGH（硬件强制要求） |

### 2.3 风扇控制

| 引脚 | 网络名 | 类型 | 功能 |
|------|--------|------|------|
| PC6 | FAN_VCC_CTRL | OUTPUT HIGH | 风扇 12V 电源通断（MOSFET Q10） |
| PC8 | FAN_PWM_CTRL | TIM3_CH3 AF2 | 风扇 PWM 调速，20kHz，驱动 IC U15 |
| PC7 | FAN_FB | TIM8_CH2 AF3 | 风扇 FG 转速反馈（开漏，内部上拉） |

**转速换算**：`RPM = fan_freq_hz × 20`（极对数 3）

**调速接口**（duty = 0~50，对应 0%~100%，ARR = 49）：

```c
__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, duty);
```

> ⚠️ 建议外接 10kΩ 上拉电阻到 3.3V 以增强 FG 信号质量。

### 2.4 压缩机（YSJ）控制

| 引脚 | 网络名 | 类型 | 功能 |
|------|--------|------|------|
| PB5 | YSJ_PWM | TIM3_CH2 AF2 | PWM 调速，20kHz，PMOS 反相 |
| PB3 | DIR_CTRL | OUTPUT | 换向（LOW=正转，HIGH=反转） |
| PD7 | BREAK_CTRL | OUTPUT | 刹车（LOW=制动，HIGH=释放） |
| PB4 | SC_COUNT | TIM3_CH1 AF2 | 转速脉冲反馈输入捕获 |

**⚠️ PMOS 反相**：TIM3_CH2 极性配置为 `TIM_OCPOLARITY_LOW`，实际输出与写入占空比逻辑相反。

**转速换算**：`RPM = sc_freq_hz × 10`（极对数 6）

**调速接口**（duty = 0~49，0 = 停止，49 = 最高速）：

```c
__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, duty);
```

**标准启停流程**：

```
启动：PD7 HIGH（释放刹车）→ 设置占空比 → HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2)
停止：占空比 = 0 → HAL_TIM_PWM_Stop → PD7 LOW（施加刹车）
```

### 2.5 NTC 温度采集（ADC1 + DMA2 循环扫描）

| 引脚 | ADC 通道 | 网络名 | adc_buf 索引 |
|------|----------|--------|--------------|
| PA0 | ADC1_IN0 | 2-NTC2_SG | `adc_buf[0]` |
| PA1 | ADC1_IN1 | 2-NTC3_SG | `adc_buf[1]` |
| PA2 | ADC1_IN2 | 2-NTC4_SG | `adc_buf[2]` |
| PA3 | ADC1_IN3 | 2-NTC1_SG | `adc_buf[3]` |
| PA4 | ADC1_IN4 | 1-NTC2_SG | `adc_buf[4]` |
| PA5 | ADC1_IN5 | 1-NTC3_SG | `adc_buf[5]` |
| PA6 | ADC1_IN6 | 1-NTC4_SG | `adc_buf[6]` |
| PA7 | ADC1_IN7 | 1-NTC1_SG | `adc_buf[7]` |

配置：12位分辨率，DMA 循环模式，480周期采样，软件触发连续转换。

**读取方式**（DMA 自动刷新，任意时刻直接读）：

```c
extern uint16_t adc_buf[8];
uint16_t raw = adc_buf[3];  // 2号组第1路 NTC 原始值（0~4095）
```

**NTC 转温度框架**（当前输出原始值，待实现）：

```c
float adc_to_celsius(uint16_t raw) {
    // 1. raw → 分压电压
    // 2. 分压电压 → NTC 电阻值
    // 3. NTC 电阻 → 温度（B 值公式或查表）
    // 需从硬件确认：分压电阻值、NTC 标称阻值、B 值
}
```

### 2.6 INA226 功率监测（I2C，3 路）

| I2C | 引脚 | HAL 地址 | 测量对象 | Rshunt | Current_LSB |
|-----|------|----------|----------|--------|-------------|
| I2C1 | PB6/PB7 | 0x88 | 水泵（U13，24V_YSJ_SENSOR） | 100mΩ | 0.1mA |
| I2C2 | PB10/PB11 | 0x88 | 24V 输入（U11，12V_VCC_UIP） | 6mΩ | 1mA |
| I2C3 | PA8/PC9 | 0x88 | 总输入功率（U12，24V_BAT_UIP） | 6mΩ | 1mA |

配置寄存器 0x00 = `0x4527`：16次平均，1.1ms 采样，连续测量模式。

**读取接口**（每 500ms 更新一次）：

```c
extern INA226_Device sensors[3];
// sensors[0].v / .i / .p  →  水泵电压/电流/功率
// sensors[1].v / .i / .p  →  24V 输入
// sensors[2].v / .i / .p  →  总输入功率
```

### 2.7 其他 IO

| 引脚 | 网络名 | 类型 | 功能 |
|------|--------|------|------|
| PA15 | BEEP_CTRL | TIM2_CH1 AF1 | 被动蜂鸣器 PWM |
| PD13 | LED1 | OUTPUT | 板载 LED1 |
| PD14 | LED2 | OUTPUT | 板载 LED2 |
| PD5 | USART2_TX | AF7 | 调试串口发送 |
| PD6 | USART2_RX | AF7 | 调试串口接收 |

---

## 三、软件架构

### 3.1 外设与定时器分配

| 外设 | 引脚 | 用途 | 时钟配置 |
|------|------|------|----------|
| TIM2_CH1 | PA15 | 蜂鸣器可变频率 PWM | APB1 84MHz，PSC=83 → 1MHz |
| TIM3_CH1 | PB4 | 压缩机转速输入捕获 | 同上，ARR=49（20kHz 基准） |
| TIM3_CH2 | PB5 | 压缩机 PWM 输出，20kHz | 同上 |
| TIM3_CH3 | PC8 | 风扇 PWM 输出，20kHz | 同上 |
| TIM8_CH2 | PC7 | 风扇 FG 输入捕获 | APB2 168MHz，PSC=167 → 1MHz |
| ADC1+DMA2 | PA0~PA7 | 8路 NTC 循环扫描 | PCLK2/4 = 21MHz |
| I2C1/2/3 | — | INA226 × 3 | APB1 |
| USART2 | PD5/PD6 | 调试串口，115200bps | APB1 |

### 3.2 中断优先级

| 中断向量 | 优先级 | 作用 |
|----------|--------|------|
| TIM3_IRQn | 1-0 | 压缩机转速捕获 + 溢出计数 |
| TIM8_UP_TIM13_IRQn | 1-0 | 风扇 FG 溢出计数 |
| TIM8_CC_IRQn | 1-0 | 风扇 FG 捕获 |
| SysTick | 默认 | HAL_Delay / HAL_GetTick |

### 3.3 转速测量原理（输入捕获，两次上升沿法）

```
timer 时钟 = 1MHz（tick = 1µs）

ticks = overflow_count × ARR_period + (val2 - val1)
频率(Hz) = 1_000_000 / ticks
```

| 通道 | ARR_period | 超时阈值 | 超时时间 |
|------|------------|----------|----------|
| TIM3 CH1（压缩机） | 50 ticks | 1000 次溢出 | ~50ms |
| TIM8 CH2（风扇） | 65536 ticks | 100 次溢出 | ~6.5s |

超时时将对应频率变量清零，上层逻辑可据此判断信号缺失。

### 3.4 主循环流程

```
上电 → 外设初始化 → INA226 配置 → 受控上电时序
     → 大疆开机音 + LED → 风扇启动 → 压缩机启动
     → 输入捕获启动
     → 主循环（500ms）：
           INA226 读取 → 打印功率
           打印风扇/压缩机转速
           打印 8 路 NTC 原始值
```

---

## 四、关键全局变量

```c
// 转速（中断更新，主循环只读）
extern volatile uint32_t sc_freq_hz;   // 压缩机反馈 Hz，RPM = × 10
extern volatile uint32_t fan_freq_hz;  // 风扇反馈 Hz，RPM = × 20

// NTC（DMA 自动刷新，任意时刻可读）
extern uint16_t adc_buf[8];            // 原始 ADC 值，0~4095

// 功率（主循环每 500ms 更新）
extern INA226_Device sensors[3];       // .v(V) / .i(A) / .p(W)
```

**当前版本 RAM 地址（map 文件）**：

| 变量 | 地址 |
|------|------|
| `sc_freq_hz` | `0x20000010` |
| `fan_freq_hz` | `0x20000020` |
| `adc_buf[0]` | `0x200001BC` |
| `sensors[0].v` | `0x20000028` |

---

## 五、后续开发接口

### 5.1 NTC → 温度转换（高优先级）

在主循环中实现并调用：

```c
float adc_to_celsius(uint16_t raw);
```

所需硬件参数（需从原理图确认）：
- 分压电阻值（上/下拉）
- NTC 标称阻值（25°C）
- NTC B 值

### 5.2 保护逻辑状态机

建议新建 `Core/Src/protection.c`，读取现有全局变量，实现：

| 触发条件 | 建议动作 |
|----------|----------|
| 任意 NTC 超温阈值 | 提高风扇占空比 / 降低压缩机占空比 / 蜂鸣报警 |
| `sc_freq_hz == 0` 持续超时 | 压缩机失速保护，停机 |
| `sensors[x].i` 超过额定值 | 对应路降载或断电 |
| `fan_freq_hz == 0` 持续超时 | 风扇失速报警 |

### 5.3 蜂鸣器报警封装

基于当前 TIM2 PWM 驱动，建议封装：

```c
// 播放指定频率报警音，可设置重复次数
void Beep_Alert(uint32_t freq_hz, uint32_t on_ms, uint32_t off_ms, uint8_t repeat);
```

### 5.4 串口指令接收

当前串口仅单向输出。建议在 USART2 增加接收中断，实现：
- 实时修改风扇/压缩机占空比
- 查询各路传感器数值
- 手动触发/解除保护

### 5.5 上位机数据协议

当前输出为人类可读文本，与上位机对接建议改为结构化帧：

```
帧格式建议：[0xAA][LEN][CMD][DATA...][CRC16][0x55]
```

---

## 六、已知限制

| 编号 | 问题 | 建议 |
|------|------|------|
| L1 | 风扇 FG 使用内部上拉（~40kΩ），低速信号偏弱 | PC7 外接 10kΩ 上拉至 3.3V |
| L2 | 压缩机 PWM 当前固定 30%，无闭环 | 保护逻辑联动后实现 PID 或分段控制 |
| L3 | NTC 当前输出原始 ADC 值，未转温度 | 补充 B 值参数后实现转换 |
| L4 | INA226 校准值为理论计算，未实测校验 | 用标准电流源/万用表比对后修正 `cal_val` |
| L5 | 主循环 500ms 轮询，无 RTOS | 保护响应延迟最大 500ms，如需更快响应需改中断驱动 |
| L6 | `printf` 依赖 MicroLib | 关闭 MicroLib 时改用 `snprintf + HAL_UART_Transmit` |

---

## 七、编译与烧录

```bash
# 编译
"C:/Keil_v5/UV4/UV4.exe" -j0 -b "MDK-ARM/upboard.uvprojx"

# 检查结果（应显示 0 Error(s)）
grep "Error(s)" MDK-ARM/upboard/upboard.build_log.htm

# 烧录
ST-LINK_CLI.exe -c SWD -P "MDK-ARM/upboard/upboard.hex" -V after_programming -Rst

# 运行时内存读取（不中断 CPU）
ST-LINK_CLI.exe -c SWD HotPlug -r32 <address> <word_count>
```

---

## 八、文件结构

```
upboard/
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── gpio.h
│   │   ├── i2c.h
│   │   └── usart.h
│   └── Src/
│       ├── main.c          ← 主逻辑、外设初始化、INA226、TIM、ADC
│       ├── gpio.c          ← 所有 GPIO 配置
│       ├── i2c.c           ← I2C1/2/3 初始化
│       ├── usart.c         ← USART2 初始化
│       └── stm32f4xx_it.c  ← TIM3/TIM8 中断处理
├── MDK-ARM/
│   ├── upboard.uvprojx     ← Keil 工程文件
│   └── upboard/
│       ├── upboard.hex     ← 烧录固件
│       ├── upboard.map     ← 符号地址表
│       └── upboard.build_log.htm
└── DEV_DOC.md              ← 本文档
```
