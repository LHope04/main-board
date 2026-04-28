# 摩托车液冷主控板 upboard

> 状态：🚧 OTA Phase 7 已完成（2026-04-18），下一步双向 OTA 联调 / CI 自动化
> 目录规范化：业务模块（buzzer/esp_comm/fan_ctrl/compressor_ctrl/sensor_acq/ina226/power_ctrl/ota）已从 `Core/Src,Inc` 迁至 `app/Src,Inc`（2026-04-20），Core/ 仅留 CubeMX 生成的 wiring

## 项目概述
STM32F407VET6 摩托车液冷系统主控板：受控上电、风扇/压缩机 PWM 控制、8 路 NTC 采集、3 路 INA226 功率监测、蜂鸣器、BLE OTA（带回滚）。

业务接口以源代码 + 头文件注释为准；硬件映射见 `.context/hardware.context.md`；历史决策见 `.context/decisions.log.md`。

---

## 开发规格
（按 skill-1 模板回填新功能时使用）

---

## 模块状态

| 模块 | 状态 | 验证方式 | 遗留问题 |
|------|------|----------|----------|
| 受控上电 power_ctrl | ✅ | 上板按时序拉高 PB13/PB14/PB12/PB15 | — |
| 风扇控制 fan_ctrl | ✅ | TIM3_CH3 PWM + TIM8_CH2 FG 捕获 | PC7 建议外接 10kΩ 上拉（L1） |
| 压缩机控制 compressor_ctrl | ✅ | TIM3_CH2 PWM（PMOS 反相）+ TIM3_CH1 IC | 未做闭环（L2） |
| NTC 采集 sensor_acq | ✅ | ADC1+DMA2 8 通道循环扫描 | 仍为原始 ADC 值，未转温度（L3） |
| 功率监测 INA226×3 | ✅ | I2C1/2/3 各一路，500ms 轮询 | cal_val 未实测校准（L4） |
| 蜂鸣器 buzzer | ✅ | TIM2_CH1，开机大疆音 | — |
| 充电/水泵使能 | ✅ | PB12/PB15 HIGH | — |
| 保护逻辑 protection | ⬜ | — | 待开发，依赖 NTC→℃ 转换 |
| Bootloader（OTA Phase 1~5） | ✅ | 0x08000000 独立工程，A/B 槽跳转 + LED 指示 | — |
| OTA 协议（Phase 6） | ✅ | BLE→ESP32-C3→USART2 端到端通过（2026-04-18） | — |
| OTA 回滚（Phase 7） | ✅ | 4 场景验证：正常/boot_count/CRC/稳态 | 双向 OTA 联调 + CI 待做 |

状态说明：⬜ 未开始 · 🚧 开发中 · ✅ 已验证

---

## 资源用量

| 时间点 | Flash (App) | RAM | 备注 |
|--------|-------------|-----|------|
| 基线（CubeMX 空工程） | — | — | 项目早于 skill 系统建立，无基线 |
| 当前 App（2026-04-18） | 16.86 KB / 128 KB（Slot 容量，13%） | 2.51 KB / 192 KB | RO=16600+660，RW=156，ZI=2412 |
| 当前 Bootloader | 5.26 KB / 128 KB（BL 区） | — | 0x08000000~0x0801FFFF |
| 上限 | 128 KB / Slot | 192 KB | F407VE 总 Flash 512 KB，OTA 布局占前 384 KB（App Slot 各 128 KB） |

Flash 布局：
- `0x08000000`：Bootloader（128 KB 区域，实际 5.3 KB）
- `0x08008000`：Boot 参数区（68 字节 × 2 副本，sequence 仲裁）
- `0x08020000`：App Slot A（128 KB）
- `0x08040000`：App Slot B（128 KB，preset `app-b` → `upboard_B.hex`）

---

## 当前 TODO
- [ ] OTA Phase 7 收尾：双向 OTA 联调（运行 B 时烧 A，再烧回 B）
- [ ] CI 自动化：cmake --build → arm-none-eabi-objcopy → bin → crc 记录（CMake POST_BUILD 已生成 hex/bin，CRC 落到 `make_params.py`，仍缺 CI workflow 串接）
- [ ] NTC 原始值 → ℃（B 值 / 上拉电阻参数待硬件确认）
- [ ] 保护状态机 protection.c（依赖上一项）

---

## 已知问题
| 编号 | 问题 | 建议 |
|------|------|------|
| L1 | 风扇 FG 内部上拉信号偏弱 | PC7 外接 10kΩ↑3.3V |
| L2 | 压缩机无闭环调速 | 上线保护逻辑后再做 PID |
| L3 | NTC 仅输出原始 ADC 值 | 补 B 值/电阻后实现 |
| L4 | INA226 校准为理论值 | 标准电流源比对 |
| L5 | 主循环 500ms 轮询，保护响应延迟最大 500ms | 如需更快需改中断驱动 |
| L6 | `printf` 在 newlib-nano 下默认无输出 | 改 `snprintf + uart_send_string`，或在 syscalls.c 实现 `_write`（见 hal-patterns.md） |

OTA 相关坑（已解决，在 decisions.log.md）：
- VTOR 不能用来判槽位 → 用 `RTC->BKP0R` 标记
- OTA 期间 STATUS 帧污染 ACK → `OtaProto_IsBusy()` 抑制
- Buzzer 生日歌 ≈10s 超 IWDG 4s → 内部喂狗
- I2C GPIO 模式切换会永久 BUSY（见 auto-memory）

---

## 工程基线状态
- 工具链：CMake 3.31.10 + Ninja 1.13.1 + arm-none-eabi-gcc 10.3.1 + OpenOCD 0.12.0 + ST-LINK_CLI（旧 Keil MDK 已弃用并删除，仓库内不再保留 `MDK-ARM/`）
- Preset / 产物：
  - Bootloader：preset `bootloader` → `build/bootloader/bootloader/bootloader.{elf,hex,bin,map}` (0x08000000)
  - App Slot A：preset `app-a` → `build/app-a/app/upboard_A.{elf,hex,bin,map}` + `params_A.bin` (0x08020000)
  - App Slot B：preset `app-b` → `build/app-b/app/upboard_B.{elf,hex,bin,map}` + `params_B.bin` (0x08040000)
- 链接脚本：`cmake/ld/STM32F407_{BOOTLOADER,APP_A,APP_B}.ld`
- 工具链文件：`cmake/arm-none-eabi-gcc.cmake`
- 当前最近一次编译：✅ 0 Error 0 Warning（三 preset 全编通过）
- 烧录验证：✅（GCC 版 bootloader + App A 在板上跑通，温度 0–1℃ 确认 Slot A）
- VSCode 调试：F5 → Debug Bootloader / Debug App A / Debug App B / Attach（Cortex-Debug + OpenOCD）
- 串口冒烟：✅（USART2，PD5/PD6，115200，归 ESP32 使用，bootloader 禁明文）
