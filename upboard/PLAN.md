# 摩托车智能降温设备软件联调与硬件实测 Plan

## Summary
目标是先打通“控制板 MCU 控制驱动板电源链路”的最小可运行闭环，再逐步扩展到风扇、采样和保护逻辑。当前默认前提是控制板和驱动板都可上电，首轮优先验证主电源链路，计划按固件模块分层组织，确保软件开发和台架实测同步推进。

## GPIO 引脚映射（网表确认，2026-03-25）

| 信号 | STM32 引脚 | 方向 | 备注 |
|------|-----------|------|------|
| EN_DVCC_5TO3V3 | PD11 | 输出 PP | 已拉高（解锁运放）✓ |
| EN_AVCC_5TO3V3 | PD12 | 输出 PP | 已拉高（解锁运放）✓ |
| EN_TPS43060 | PB13 | 输出 PP | 驱动板主升压使能 |
| EN_24TO12 | PB14 | 输出 PP | 风扇 12V 电源使能 |
| FAN_VCC_CTRL | PC6 | 输出 PP | 风扇供电通断开关 |
| FAN_FB_OUT | PC7 | 输入捕获 | 风扇测速反馈 |
| FAN_PWM_CTRL | PC8 | TIM3_CH3 PWM | 风扇调速 |
| BEEP_CTRL | PA15 | 输出 PP | 蜂鸣器 |
| DIR_CTRL | PB3 | 输出 PP | 方向控制 |
| BREAK_CTRL | PD7 | 输出 PP | 刹车控制 |
| I2C1 SCL/SDA | PB6/PB7 | I2C AF | 24V_YSJ_SENSOR |
| I2C2 SCL/SDA | PB10/PB11 | I2C AF | 12V_VCC_UIP |
| I2C3 SCL/SDA | PA8/PC9 | I2C AF | 24V_BAT_UIP |
| USART2 TX/RX | PD5/PD6 | UART AF | 调试串口 115200 |
| 2-NTC2_SG | PA0 | ADC1_IN0 | NTC 模拟输入（ADC） |
| 2-NTC3_SG | PA1 | ADC1_IN1 | NTC 模拟输入（ADC） |
| 2-NTC4_SG | PA2 | ADC1_IN2 | NTC 模拟输入（ADC） |
| 2-NTC1_SG | PA3 | ADC1_IN3 | NTC 模拟输入（ADC） |
| 1-NTC2_SG | PA4 | ADC1_IN4 | NTC 模拟输入（ADC） |
| 1-NTC3_SG | PA5 | ADC1_IN5 | NTC 模拟输入（ADC） |
| 1-NTC4_SG | PA6 | ADC1_IN6 | NTC 模拟输入（ADC）⚠ gpio.c 当前误配为 OUTPUT_PP |
| 1-NTC1_SG | PA7 | ADC1_IN7 | NTC 模拟输入（ADC）⚠ gpio.c 当前误配为 OUTPUT_PP |
| LED1 | PD13 | 输出 PP | 板载 LED1 |
| LED2 | PD14 | 输出 PP | 板载 LED2 |
| NETQ2 | PB12 | 输入/TP | TP16，上拉，功能 TBD |
| P11 | PB15 | FPC 直通 | FPC2.11，驱动板测试点 |
| P14 | PB1 | FPC 直通 | FPC2.14，驱动板 sensor1 |

## Key Changes
### 1. 电源控制固件模块
- 建立统一的 `power_ctrl` 模块，明确 3 类控制脚：
  - 本板电源使能：`EN_DVCC_5TO3V3`(PD11)、`EN_AVCC_5TO3V3`(PD12)
  - 下游主供电使能：`EN_TPS43060`(PB13) → 驱动板 TPS43060(U1) EN 脚，12V→24V 升压
  - 下游次级供电使能：`EN_24TO12`(PB14) → 驱动板 U5 EN 脚，24V→12V 降压（12V_VCC_FAN）
- 固定默认策略：
  - 系统复位后上述引脚全部输出为关闭态。
  - `FAN_VCC_CTRL`(PC6)、`FAN_PWM_CTRL`(PC8) 也默认关闭，避免下游误动作。
- 固化软件时序：
  1. 初始化时钟、日志、GPIO。
  2. 初始化本板 ADC/I2C，但不打开下游供电。
  3. 检查输入电源存在。
  4. 拉高 `EN_TPS43060`，等待 24V 母线稳定。
  5. 检查 24V 链路正常后再拉高 `EN_24TO12`。
  6. 后续功能测试才允许打开 `FAN_VCC_CTRL` 和 `FAN_PWM_CTRL`。
- 为每一步定义超时、失败回退和日志点，避免现场只看到“没反应”。

### 2. GPIO/PWM/反馈固件模块
- 建立 `actuator_ctrl` 模块，收口以下接口：
  - 输出：`FAN_VCC_CTRL`(PC6)、`FAN_PWM_CTRL`(PC8 TIM3_CH3)、`BEEP_CTRL`(PA15)、`DIR_CTRL`(PB3)、`BREAK_CTRL`(PD7)
  - 输入：`FAN_FB_OUT`(PC7 输入捕获)
- 驱动板拓扑：
  - `FAN_VCC_CTRL`(PC6) → Q10 栅极（控制 12V_VCC_FAN MOSFET 通断）
  - `FAN_PWM_CTRL`(PC8) → 驱动板 U15.1（风扇驱动 IC PWM 输入）
  - `FAN_FB_OUT`(PC7) → 驱动板 U14.4（风扇驱动 IC 反馈输出）
- 角色边界：
  - `FAN_VCC_CTRL` 负责负载通断（MOSFET Q10）。
  - `FAN_PWM_CTRL` 只负责调速（进风扇驱动 IC）。
  - `FAN_FB_OUT` 只用于测速/闭环，不参与上电判定。
- 首版先做开环控制与反馈采样，不先做复杂闭环调速。

### 3. ADC/I2C 采样模块
- 建立 `sensor_acq` 模块，先覆盖最关键输入：
  - 8 路 NTC（全部来自 ADC1，DMA 扫描模式）：
    | 信号 | STM32 引脚 | ADC 通道 |
    |------|-----------|---------|
    | 1-NTC1_SG | PA7 | ADC1_IN7 |
    | 1-NTC2_SG | PA4 | ADC1_IN4 |
    | 1-NTC3_SG | PA5 | ADC1_IN5 |
    | 1-NTC4_SG | PA6 | ADC1_IN6 |
    | 2-NTC1_SG | PA3 | ADC1_IN3 |
    | 2-NTC2_SG | PA0 | ADC1_IN0 |
    | 2-NTC3_SG | PA1 | ADC1_IN1 |
    | 2-NTC4_SG | PA2 | ADC1_IN2 |
  - **⚠ 阶段 3 前提**：修复 `gpio.c` 中 PA6/PA7 被误配为 `GPIO_MODE_OUTPUT_PP` 的问题，改为 ADC 模拟输入（GPIO_MODE_ANALOG）。
  - 三组 I2C INA226：
    | I2C | 引脚 | 驱动板芯片 | 测量对象 |
    |-----|------|----------|---------|
    | I2C1 | PB6/PB7 | U13 (24V_YSJ_SENSOR) | 水泵/YSJ 功率 |
    | I2C2 | PB10/PB11 | U11 (12V_VCC_UIP) | 12V 输入功率 |
    | I2C3 | PA8/PC9 | U12 (24V_BAT_UIP) | 24V 电池功率 |
- 首版策略：
  - NTC 先验证原始 ADC 值和开短路判定，再做温度换算。
  - I2C 先做总线扫描、设备在线检测、基础寄存器读写，不先做完整业务算法。
- `P11`(PB15)、`P14`(PB1) 为 FPC 直通信号，`CTRL_CHARGE_NTC` 直接接驱动板充电 IC（STM32 无控制权），均不纳入首轮关键路径。

### 4. 联调日志与故障判定
- 建立统一 bring-up 状态机和日志编码，至少区分：
  - 输入电源缺失
  - `EN_TPS43060` 后 24V 未建立
  - `EN_24TO12` 后 12V_FAN 未建立
  - 风扇反馈异常
  - I2C 设备缺失
  - NTC 开路/短路
- 每个故障都给出对应测点和建议复查项，保证软件日志能直接指导示波器/万用表排查。

## Test Plan
### 1. 主电源链路实测
- 断开风扇和大负载，仅保留控制板+驱动板。
- 上电后确认所有 `EN_*` 和 `FAN_VCC_CTRL` 默认关闭。
- 软件手动单步拉高 `EN_TPS43060`，测驱动板 24V 母线是否建立，记录建立时间和稳态电压。
- 保持 `EN_TPS43060` 有效，再拉高 `EN_24TO12`，测风扇 12V 电源是否建立。
- 任一步失败，软件立即拉低后级使能，验证回退逻辑是否生效。

### 2. GPIO/负载链路实测
- 在 24V 和 12V_FAN 已稳定的前提下，单独打开 `FAN_VCC_CTRL`，确认风扇供电链路动作正确。
- 在 `FAN_VCC_CTRL` 已打开后再输出 `FAN_PWM_CTRL`，验证占空比变化是否改变风扇行为。
- 读取 `FAN_FB_OUT`，确认反馈频率随转速变化。
- 对 `BEEP_CTRL`、`DIR_CTRL`、`BREAK_CTRL` 做单独点动测试，确认无耦合误动作。

### 3. 采样链路实测
- NTC 输入分别接标准电阻，验证 8 路 ADC 通道映射和温度区间是否合理。
- 逐组扫描三条 I2C，总线必须先过“设备存在”再进入业务读取。
- 若 I2C 设备存在但读值异常，记录地址、寄存器、原始值，不在首轮现场临时改算法。

### 4. 异常与保护测试
- 模拟 `EN_TPS43060` 打开但 24V 未建立，确认软件超时报错且不继续拉高 `EN_24TO12`。
- 模拟 `EN_24TO12` 后 12V_FAN 异常，确认风扇链路不上电。
- 模拟风扇堵转或无反馈，确认 `FAN_FB_OUT` 超时告警。
- 模拟 NTC 开路/短路，确认软件进入保护或降级状态。

## Public Interfaces / Signal Contracts
- 板间 20Pin FPC 中与首轮联调直接相关的信号固定为：
  - `EN_TPS43060` → PB13
  - `EN_24TO12` → PB14
  - `FAN_VCC_CTRL` → PC6
  - `FAN_PWM_CTRL` → PC8 (TIM3_CH3)
  - `FAN_FB_OUT` → PC7 (输入捕获)
  - I2C2(PB10/PB11)：`12V_VCC_UIP_*`
  - I2C3(PA8/PC9)：`24V_BAT_UIP_*`
  - I2C1(PB6/PB7)：`24V_YSJ_SENSOR_*`
- 固件对外保留最小调试接口：
  - 单步控制某个使能脚
  - 读取当前电源阶段状态
  - 导出 ADC 原始值和 I2C 在线状态
  - 查询故障码

## Assumptions
- 当前依据网表确认了网络连接和控制关系；个别芯片具体型号可后续再由原理图补齐，但不影响首轮软件 bring-up 计划。
- `EN_TPS43060` 视为驱动板主升压入口，`EN_24TO12` 视为风扇 12V 电源入口，这是首轮测试的默认前提。
- 首轮不追求完整业务功能，只追求“可控上电、可测反馈、可定位故障”的最小联调闭环。
