# 模块接口说明文档

本文档描述 `upboard` 固件各功能模块的公开接口、参数调整方法与典型调用方式，供后续开发者阅读与修改时参考。

所有模块均位于 `Core/Src/*.c` + `Core/Inc/*.h`，在 `main.c` 中通过调用各模块的 `Init` 函数初始化，不需要了解模块内部实现。

---

## 目录

1. [INA226 功率传感器驱动](#1-ina226-功率传感器驱动)
2. [电源控制模块 power_ctrl](#2-电源控制模块-power_ctrl)
3. [风扇控制模块 fan_ctrl](#3-风扇控制模块-fan_ctrl)
4. [压缩机控制模块 compressor_ctrl](#4-压缩机控制模块-compressor_ctrl)
5. [传感器采集模块 sensor_acq](#5-传感器采集模块-sensor_acq)
6. [蜂鸣器模块 buzzer](#6-蜂鸣器模块-buzzer)
7. [main.c 编排说明](#7-mainc-编排说明)
8. [常见参数速查表](#8-常见参数速查表)

---

## 1. INA226 功率传感器驱动

**文件：** `Core/Src/ina226.c` / `Core/Inc/ina226.h`

### 数据结构

```c
typedef struct {
    I2C_HandleTypeDef *hi2c;   // I2C 句柄指针（&hi2c1 / &hi2c2 / &hi2c3）
    uint8_t            dev_addr; // I2C 地址，HAL 惯例左移一位，例：0x40 → 0x80
    uint16_t           cal_val;  // 校准寄存器值，决定电流量程
    float              cur_lsb;  // 电流 LSB，单位 A（例：0.0001 = 0.1mA/LSB）
    float              voltage_V; // 最近一次读取的总线电压（V）
    float              current_A; // 最近一次读取的电流（A）
    float              power_W;   // 最近一次读取的功率（W）
} INA226_Device;
```

> **字段只读：** `voltage_V / current_A / power_W` 由 `INA226_UpdateData()` 填入，外部只读，不要直接写入。

### 公开函数

#### `INA226_Init`
```c
void INA226_Init(INA226_Device *dev,
                 I2C_HandleTypeDef *hi2c,
                 uint8_t  addr,   // 设备 I2C 地址（已左移，例 0x88）
                 uint16_t cal,    // 校准寄存器值（计算见下文）
                 float    lsb);   // 电流 LSB（A）
```
完成结构体赋值并向芯片写入 Config 寄存器（0x4527）和校准寄存器。

**校准值计算公式：**
```
Current_LSB = 最小可分辨电流（A），建议取量程/32768
CAL = 0.00512 / (Current_LSB × Rshunt_Ω)
```

| 通道 | Rshunt | Current_LSB | cal_val |
|------|--------|-------------|---------|
| I2C1（泵） | 100 mΩ | 0.1 mA = 0.0001 A | 0x0200 (512) |
| I2C2（24V输入） | 6 mΩ | 1.2 mA = 0.0012 A | 0x0355 (853) |
| I2C3（总功率） | 6 mΩ | 1.2 mA = 0.0012 A | 0x0355 (853) |

---

#### `INA226_UpdateData`
```c
HAL_StatusTypeDef INA226_UpdateData(INA226_Device *dev);
```
通过 I2C 读取电压（寄存器 0x02）、电流（0x04）、功率（0x03）并更新结构体字段。
返回 `HAL_OK`（实际 I2C 错误目前被内部忽略，后续可扩展错误处理）。

**调用频率建议：** 芯片配置为 16 次平均 + 1.1ms 转换，完整一轮约 17.6ms，500ms 主循环调用绰绰有余。

---

#### 读取函数
```c
float INA226_GetVoltage(const INA226_Device *dev);  // 返回电压（V）
float INA226_GetCurrent(const INA226_Device *dev);  // 返回电流（A），可为负
float INA226_GetPower(const INA226_Device *dev);    // 返回功率（W）
```

### 典型用法
```c
INA226_Device sensor;
INA226_Init(&sensor, &hi2c1, 0x88, 0x0200, 0.0001f);

// 主循环中
INA226_UpdateData(&sensor);
printf("V=%.3f I=%.3f P=%.3f\r\n",
       INA226_GetVoltage(&sensor),
       INA226_GetCurrent(&sensor),
       INA226_GetPower(&sensor));
```

### 参数调整指引

| 需要改动 | 修改位置 |
|---------|---------|
| 更换采样电阻 | 重新计算 `cal` 和 `lsb`，更新 `INA226_Init()` 调用参数 |
| 修改平均次数/转换时间 | `ina226.c` 中 `INA226_WriteReg(dev, 0x00, 0x4527)` 一行，参考 INA226 数据手册 Table 7 |
| I2C 地址变更（A0/A1 接法） | 计算新地址后左移 1 位，更新 `addr` 参数 |

---

## 2. 电源控制模块 power_ctrl

**文件：** `Core/Src/power_ctrl.c` / `Core/Inc/power_ctrl.h`

### 硬件映射

| 信号 | 引脚 | 功能 | 有效电平 |
|------|------|------|---------|
| EN_TPS43060 | PB13 | 12V→24V 升压使能 | HIGH |
| EN_24TO12 | PB14 | 24V→12V 降压使能（12V_VCC_FAN 供电）| HIGH |
| CHARGE_EN | PB12 | 锂电池充电模块使能 | HIGH |
| PUMP_EN | PB15 | 水泵输出使能 | HIGH |

所有引脚上电默认 LOW（在 `gpio.c` 中已配置），此模块只负责按顺序拉高。

### 公开函数

#### `PowerCtrl_StartupSequence`
```c
void PowerCtrl_StartupSequence(void);
```
按安全时序依次使能各路电源：

```
PB13=1 → 等待 500ms（24V 总线建立）
PB14=1 → 等待 200ms（12V_FAN 建立）
PB12=1 → 等待 50ms
PB15=1 → 等待 50ms
```

> **调整时序：** 直接修改 `power_ctrl.c` 中对应的 `HAL_Delay()` 参数（单位 ms）。

---

#### 单路控制函数
```c
void PowerCtrl_EnableBoost(uint8_t en);    // PB13: 1=开启 0=关闭
void PowerCtrl_EnableBuck(uint8_t en);     // PB14: 1=开启 0=关闭
void PowerCtrl_EnableCharger(uint8_t en);  // PB12: 1=开启 0=关闭
void PowerCtrl_EnablePump(uint8_t en);     // PB15: 1=开启 0=关闭
```

### 典型用法
```c
// 上电
PowerCtrl_StartupSequence();

// 运行时关闭水泵
PowerCtrl_EnablePump(0);

// 重新开启水泵
PowerCtrl_EnablePump(1);
```

---

## 3. 风扇控制模块 fan_ctrl

**文件：** `Core/Src/fan_ctrl.c` / `Core/Inc/fan_ctrl.h`

### 硬件映射

| 信号 | 引脚 | 定时器 | 功能 |
|------|------|-------|------|
| FAN_VCC_CTRL | PC6 | GPIO OUTPUT | 12V 风扇电源开关（HIGH=on） |
| FAN_PWM_CTRL | PC8 | TIM3_CH3 AF2 | 风扇调速 PWM（20kHz） |
| FAN_FB（FG） | PC7 | TIM8_CH2 AF3 | 风扇转速反馈（开漏，内部上拉） |

**FG 信号规格：** 每转 3 个脉冲（3 极对），有效频率范围 1~1000 Hz（低于 1Hz 超时置零，高于 1000Hz 视为 PWM 串扰丢弃）。

### 公开函数

#### `FanCtrl_Init`
```c
void FanCtrl_Init(TIM_HandleTypeDef *htim_pwm,   // TIM3（已由 MX_TIM3_Init 初始化）
                  TIM_HandleTypeDef *htim_ic);    // TIM8（已由 MX_TIM8_Init 初始化）
```
存储句柄，启动 TIM8_CH2 输入捕获中断和溢出中断。
**注意：不重新初始化定时器底层配置，MX_TIM3_Init / MX_TIM8_Init 必须在此之前调用。**

---

#### `FanCtrl_Enable`
```c
void FanCtrl_Enable(uint8_t en);   // 1=PC6=HIGH（供电），0=PC6=LOW（断电）
```

---

#### `FanCtrl_SetDuty`
```c
void FanCtrl_SetDuty(uint8_t percent);   // 0~100，单位 %
```
计算公式：`CCR = (ARR+1) × percent / 100`，其中 ARR=49（由 MX_TIM3_Init 配置）。

| percent | CCR | 实际行为 |
|---------|-----|---------|
| 0 | 0 | 停止（PWM 低电平锁定） |
| 50 | 25 | 50% 占空比 |
| 100 | 50 | 全速 |

> **调整 PWM 频率：** 修改 `MX_TIM3_Init()` 中的 `htim3.Init.Period`（ARR）。ARR = 1MHz / PWM_freq - 1。

---

#### 读取函数
```c
float    FanCtrl_GetFreqHz(void);   // FG 测量频率（Hz），0=信号丢失
uint32_t FanCtrl_GetRPM(void);      // 转速（RPM）= freq × 20
```

**RPM 公式由来：** 3 极对 → 每转 3 个脉冲 → RPM = Hz × 60 / 3 = Hz × 20
如果换用不同风扇，修改 `fan_ctrl.c` 中 `GetRPM()` 的乘数。

---

#### 回调函数（在 main.c 的 HAL 回调中调用，不要直接调用）
```c
void FanCtrl_CaptureCallback(TIM_HandleTypeDef *htim);
void FanCtrl_OverflowCallback(TIM_HandleTypeDef *htim);
```

### 典型用法
```c
FanCtrl_Init(&htim3, &htim8);
FanCtrl_Enable(1);
FanCtrl_SetDuty(50);   // 50% 速度启动

// 主循环中
printf("FAN: %lu Hz  %lu RPM\r\n", (uint32_t)FanCtrl_GetFreqHz(), FanCtrl_GetRPM());
```

### 参数调整指引

| 需要改动 | 修改位置 |
|---------|---------|
| 改变风扇极对数（RPM 系数） | `fan_ctrl.c` → `FanCtrl_GetRPM()` 中的 `* 20UL` |
| 修改 FG 有效频率上限（抗串扰阈值） | `fan_ctrl.c` → `FanCtrl_CaptureCallback()` 中 `f <= 1000U` |
| 修改 FG 超时轮数（信号丢失判定） | `fan_ctrl.c` → `FanCtrl_OverflowCallback()` 中 `> 100U`（每轮 65.5ms） |
| FG 输入滤波强度 | `MX_TIM8_Init()` 中 `icConfig.ICFilter = 0x0F`（0x00=无滤波 0x0F=最强） |

---

## 4. 压缩机控制模块 compressor_ctrl

**文件：** `Core/Src/compressor_ctrl.c` / `Core/Inc/compressor_ctrl.h`

### 硬件映射

| 信号 | 引脚 | 定时器/GPIO | 功能 |
|------|------|------------|------|
| YSJ_PWM | PB5 | TIM3_CH2 AF2 | 压缩机调速 PWM（PMOS 驱动，极性反转） |
| DIR_CTRL | PB3 | GPIO OUTPUT | 方向控制（0=正转，1=反转） |
| BREAK_CTRL | PD7 | GPIO OUTPUT | 刹车（1=释放刹车可转，0=刹死） |
| SC_COUNT | PB4 | TIM3_CH1 AF2 | 压缩机速度脉冲输入 |

**TIM3 被 fan_ctrl（CH3）和 compressor_ctrl（CH1/CH2）共用，不重复初始化。**

### PMOS 极性说明

TIM3_CH2 配置为 `TIM_OCPOLARITY_LOW`（极性取反）。
`SetDuty(30)` 时 CCR=15，实际 MOSFET 通断比等效为 30% 有效功率输出。
直接用百分比设定即可，内部已自动处理。

### 公开函数

#### `CompressorCtrl_Init`
```c
void CompressorCtrl_Init(TIM_HandleTypeDef *htim);   // TIM3
```
存储句柄，启动 TIM3_CH1 输入捕获中断和溢出中断。

---

#### `CompressorCtrl_SetDuty`
```c
void CompressorCtrl_SetDuty(uint8_t percent);   // 0~100，单位 %
```
设定压缩机转速，同时启动 TIM3_CH2 PWM 输出。

| percent | 说明 |
|---------|------|
| 0 | 停止 |
| 30 | 建议最低启动转速 |
| 100 | 最大转速 |

---

#### `CompressorCtrl_SetDirection`
```c
void CompressorCtrl_SetDirection(uint8_t dir);   // 0=正转，1=反转
```

---

#### `CompressorCtrl_SetBrake`
```c
void CompressorCtrl_SetBrake(uint8_t en);   // 1=释放刹车（允许转动），0=刹死
```
正常运行前必须调用 `SetBrake(1)` 释放刹车。

---

#### 读取函数
```c
float    CompressorCtrl_GetFreqHz(void);   // SC_COUNT 脉冲频率（Hz），0=信号丢失
uint32_t CompressorCtrl_GetRPM(void);      // 转速（RPM）= freq × 10
```

**RPM 公式由来：** 6 极对 → 每转 6 个脉冲 → RPM = Hz × 60 / 6 = Hz × 10
如果换用不同电机，修改 `compressor_ctrl.c` 中 `GetRPM()` 的乘数。

---

#### 回调函数（在 main.c 的 HAL 回调中调用）
```c
void CompressorCtrl_CaptureCallback(TIM_HandleTypeDef *htim);
void CompressorCtrl_OverflowCallback(TIM_HandleTypeDef *htim);
```

### 典型用法
```c
CompressorCtrl_Init(&htim3);
CompressorCtrl_SetDirection(0);   // 正转
CompressorCtrl_SetBrake(1);       // 释放刹车
CompressorCtrl_SetDuty(30);       // 30% 起转

// 主循环中
printf("YSJ: %lu Hz  %lu RPM\r\n",
       (uint32_t)CompressorCtrl_GetFreqHz(), CompressorCtrl_GetRPM());
```

### 参数调整指引

| 需要改动 | 修改位置 |
|---------|---------|
| 改变极对数（RPM 系数） | `compressor_ctrl.c` → `CompressorCtrl_GetRPM()` 中的 `* 10UL` |
| SC_COUNT 超时时间 | `CompressorCtrl_OverflowCallback()` 中 `> 1000U`（每轮 50µs，1000轮=50ms） |
| SC_COUNT 输入滤波 | `MX_TIM3_Init()` 中 `icConfig.ICFilter`（0x00~0x0F） |

---

## 5. 传感器采集模块 sensor_acq

**文件：** `Core/Src/sensor_acq.c` / `Core/Inc/sensor_acq.h`

### 硬件映射

ADC1 扫描模式，DMA2 Stream0 循环传输，8 通道连续采集（480 周期采样，12bit 分辨率）：

| adc_buf 下标 | 引脚 | ADC 通道 | NTC 信号名 |
|:-----------:|------|---------|-----------|
| 0 | PA0 | IN0 | 2-NTC2_SG |
| 1 | PA1 | IN1 | 2-NTC3_SG |
| 2 | PA2 | IN2 | 2-NTC4_SG |
| 3 | PA3 | IN3 | 2-NTC1_SG |
| 4 | PA4 | IN4 | 1-NTC2_SG |
| 5 | PA5 | IN5 | 1-NTC3_SG |
| 6 | PA6 | IN6 | 1-NTC4_SG |
| 7 | PA7 | IN7 | 1-NTC1_SG |

> **注意：** PA6/PA7 在当前 `gpio.c` 中已正确配置为 `GPIO_MODE_ANALOG`，采集正常。

### 公开函数

#### `SensorAcq_Init`
```c
void SensorAcq_Init(ADC_HandleTypeDef *hadc);   // 传入 &hadc1
```
存储 ADC 句柄指针，DMA 目标为模块内部 `static uint16_t s_adc_buf[8]`（外部不可直接访问）。

---

#### `SensorAcq_Start`
```c
void SensorAcq_Start(void);
```
调用 `HAL_ADC_Start_DMA` 启动循环采集，之后 DMA 自动更新缓冲区，无需再次调用。
通常在上电序列完成后调用一次。

---

#### 读取函数
```c
uint16_t        SensorAcq_GetNTC(uint8_t ch);      // ch=0~7，返回 12bit 原始值（0~4095）
const uint16_t *SensorAcq_GetAllNTC(void);         // 返回内部缓冲区指针，只读
```

**原始值转温度（外部实现）：**
```
V_ntc = raw × 3.3 / 4095          // ADC 参考电压 3.3V
R_ntc = R_上拉 × V_ntc / (3.3 - V_ntc)   // 分压公式
T = 1 / (ln(R_ntc/R25) / B + 1/298.15) - 273.15  // NTC B 参数法（K→℃）
```
R25 和 B 参数查阅所用 NTC 规格书。

### 典型用法
```c
SensorAcq_Init(&hadc1);
SensorAcq_Start();

// 主循环中（DMA 自动更新，直接读即可）
const uint16_t *ntc = SensorAcq_GetAllNTC();
printf("NTC[0]=%d NTC[7]=%d\r\n", ntc[0], ntc[7]);

// 或按信号名读取
uint16_t ntc1_zone1 = SensorAcq_GetNTC(7);   // 1-NTC1_SG
```

### 参数调整指引

| 需要改动 | 修改位置 |
|---------|---------|
| 采样周期（精度 vs 速度） | `MX_ADC1_Init()` 中 `sConfig.SamplingTime`，可选值见 HAL 头文件 `ADC_SAMPLETIME_*` |
| 通道数量（扩展或裁减 NTC） | `MX_ADC1_Init()` 中 `hadc1.Init.NbrOfConversion` 和 channel 配置循环 |
| ADC 分辨率 | `hadc1.Init.Resolution`，可选 12/10/8/6 bit |

---

## 6. 蜂鸣器模块 buzzer

**文件：** `Core/Src/buzzer.c` / `Core/Inc/buzzer.h`

### 硬件映射

| 信号 | 引脚 | 定时器 | 备注 |
|------|------|-------|------|
| BEEP_CTRL | PA15 | TIM2_CH1 AF1 | 无源蜂鸣器，需 PWM 驱动 |

**频率计算：** TIM2 PSC=83，APB1 时钟 84MHz → 计数频率 1MHz
`ARR = 1_000_000 / freq_hz - 1`，占空比固定 50%（`CCR = ARR / 2`）

### 公开函数

#### `Buzzer_Init`
```c
void Buzzer_Init(TIM_HandleTypeDef *htim);   // 传入 &htim2
```

---

#### `Buzzer_PlayTone`
```c
void Buzzer_PlayTone(uint32_t freq_hz,     // 音调频率（Hz），建议范围 200~4000；传 0 直接返回
                     uint32_t duration_ms); // 持续时间（ms）
```
阻塞式，返回时已停止输出。`freq_hz=0` 时函数直接返回，不产生任何输出。

---

#### `Buzzer_Stop`
```c
void Buzzer_Stop(void);
```
立即停止当前音调。

---

#### `Buzzer_PlayDJIStartup`
```c
void Buzzer_PlayDJIStartup(void);
```
播放大疆无人机开机提示音（约 1.8 秒），仅控制蜂鸣器，不操作任何 GPIO。

**音符序列：**

| 阶段 | 音符 | 频率 | 时长 |
|------|------|------|------|
| ESC 初始化 | C5 | 523 Hz | 100ms × 3，间隔 80ms |
| 主旋律 1 | A5 | 880 Hz | 200ms |
| 主旋律 2 | C#6 | 1109 Hz | 200ms |
| 主旋律 3 | E6 | 1319 Hz | 650ms |

### 典型用法
```c
Buzzer_Init(&htim2);

// 开机音
Buzzer_PlayDJIStartup();

// 告警音（三声短促高音）
for (int i = 0; i < 3; i++) {
    Buzzer_PlayTone(2000, 100);
    HAL_Delay(100);
}
```

### 参数调整指引

| 需要改动 | 修改位置 |
|---------|---------|
| 更改开机音符 | `buzzer.c` → `Buzzer_PlayDJIStartup()` 中的 `chime_notes[]` 和 `chime_durs[]` |
| 更换 ESC 提示音频率 | `buzzer.c` → `esc_notes[]` 中非零值 |
| 添加新旋律 | 新建函数，在内部调用 `Buzzer_PlayTone()` 组合即可 |
| 修改 TIM2 基准频率（不建议） | `MX_TIM2_Init()` 中的 `Prescaler`，同时需更新频率计算公式 |

---

## 7. main.c 编排说明

`main.c` 只做外设初始化和模块编排，不含任何业务逻辑。

### 外设初始化顺序（不可随意调换）
```c
MX_GPIO_Init();        // GPIO 时钟和引脚方向必须最先
MX_TIM2_Init();        // 蜂鸣器定时器
MX_TIM3_Init();        // 风扇 PWM + 压缩机 PWM/IC（两模块共用）
MX_TIM8_Init();        // 风扇 FG 输入捕获
MX_ADC1_Init();        // ADC1 + DMA2
MX_I2C2_Init();
MX_I2C1_Init();
MX_I2C3_Init();
MX_USART2_UART_Init(); // printf 重定向
```

### 模块初始化顺序
```c
INA226_Init(...)  × 3       // 写 INA226 寄存器（I2C 已就绪）
SensorAcq_Init(&hadc1)
FanCtrl_Init(&htim3, &htim8)       // 启动 TIM8 IC 中断
CompressorCtrl_Init(&htim3)        // 启动 TIM3 IC 中断
Buzzer_Init(&htim2)
PowerCtrl_StartupSequence()        // 拉起各路电源（约 850ms）
SensorAcq_Start()                  // 启动 ADC DMA
FanCtrl_Enable(1)
FanCtrl_SetDuty(50)
CompressorCtrl_SetBrake(1)
CompressorCtrl_SetDuty(30)
Buzzer_PlayDJIStartup()            // 开机提示音（约 1.8s）
```

### HAL 回调转发规则

HAL 回调是全局弱符号，**只能有一个定义**，因此集中写在 `main.c` 并转发给各模块：

```c
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    FanCtrl_CaptureCallback(htim);
    CompressorCtrl_CaptureCallback(htim);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    FanCtrl_OverflowCallback(htim);
    CompressorCtrl_OverflowCallback(htim);
}
```

如果新增模块需要定时器回调，在此处追加转发即可，不要在模块内部重定义 HAL 回调。

---

## 8. 常见参数速查表

### 定时器配置

| 定时器 | APB 时钟 | PSC | 计数频率 | ARR | 用途 |
|-------|---------|-----|---------|-----|------|
| TIM2 | APB1 84MHz | 83 | 1 MHz | 可变 | 蜂鸣器，ARR=1MHz/Hz-1 |
| TIM3 | APB1 84MHz | 83 | 1 MHz | 49 | 风扇/压缩机 PWM，20kHz |
| TIM8 | APB2 168MHz | 167 | 1 MHz | 65535 | 风扇 FG 输入捕获 |

### RPM 换算系数

| 对象 | 极对数 | RPM 公式 | 调整位置 |
|------|--------|---------|---------|
| 风扇（PC 散热） | 3 | RPM = Hz × 20 | `fan_ctrl.c: GetRPM()` |
| 压缩机（YSJ） | 6 | RPM = Hz × 10 | `compressor_ctrl.c: GetRPM()` |

### INA226 寄存器 0x00 配置字段（0x4527）

| 位域 | 值 | 含义 |
|-----|----|------|
| AVG[2:0] | 100 | 16 次平均 |
| VBUSCT[2:0] | 100 | 总线电压转换 1.1ms |
| VSHCT[2:0] | 100 | 分流电压转换 1.1ms |
| MODE[2:0] | 111 | 连续测量（shunt + bus） |

如需更快响应，将 AVG 改为 000（1 次平均），并缩短转换时间，参考 INA226 数据手册 Table 7。

### 编译与烧录命令（Bash）

```bash
# 发现路径
KEIL=$(find /c/Keil_v5 /c/Keil -name "UV4.exe" 2>/dev/null | head -1)
PROJ=$(find . -name "*.uvprojx" 2>/dev/null | head -1)
TARGET=$(basename "$PROJ" .uvprojx)
HEX="$(dirname "$PROJ")/$TARGET/$TARGET.hex"
LOG="$(dirname "$PROJ")/$TARGET/$TARGET.build_log.htm"

# 编译
"$KEIL" -j0 -b "$PROJ"
grep -E "Error\(s\)|Warning\(s\)|Program Size" "$LOG" | tail -3

# 烧录
ST-LINK_CLI.exe -c SWD -P "$HEX" -V after_programming -Rst

# 读取变量（HotPlug 不停机）
grep "s_freq_hz" "$MAP_FILE"    # 查找符号地址
ST-LINK_CLI.exe -c SWD HotPlug -r32 <address> 1
```
