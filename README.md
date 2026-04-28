# upboard — 摩托车液冷主控板固件

STM32F407VET6 主控板：受控上电、风扇/压缩机 PWM、8 路 NTC 采集、3 路 INA226 功率监测、蜂鸣器、BLE OTA（A/B 槽 + 回滚）。

> 本文档面向**人**。AI 协作约定见 `CLAUDE.md`，会话上下文在 `.context/`，技能脚本在 `.skills/`。

---

## 目录结构

```
upboard/
├── CMakeLists.txt          顶层 CMake 入口（按 preset 拉子目录）
├── CMakePresets.json       7 个 preset：bootloader / app-a / app-b（× release/debug）
├── upboard.ioc             CubeMX 工程文件（配置时钟、引脚、外设）
│
├── bootloader/             Bootloader 固件（独立 image @ 0x08000000，32 KB）
│   ├── CMakeLists.txt
│   └── Src/main.c          A/B 槽仲裁 + CRC 校验 + 跳转 + IWDG
│
├── app/                    Application 固件（双 image：A 槽 @ 0x08020000，B 槽 @ 0x08040000）
│   ├── CMakeLists.txt      源码同一份，靠 UPBOARD_SLOT 选不同 .ld + VECT_TAB_OFFSET
│   ├── Src/                业务模块（手写）
│   │   ├── buzzer.c              蜂鸣器（开机大疆音）
│   │   ├── compressor_ctrl.c     压缩机 PWM + 转速捕获
│   │   ├── esp_comm.c            USART2 ↔ ESP32-C3 协议（含 OTA）
│   │   ├── fan_ctrl.c            风扇 PWM + FG 捕获
│   │   ├── ina226.c              I2C 功率监测驱动
│   │   ├── ota.c                 OTA 协议状态机 + Flash 写入
│   │   ├── power_ctrl.c          受控上电时序
│   │   └── sensor_acq.c          ADC1 + DMA 8 路 NTC 采集
│   └── Inc/                业务模块头文件
│
├── Core/                   CubeMX 自动生成 — 不要手改
│   ├── Src/                main.c · gpio.c · i2c.c · usart.c · stm32f4xx_it.c · _hal_msp.c · system_stm32f4xx.c
│   └── Inc/                main.h · gpio.h · i2c.h · usart.h · stm32f4xx_it.h · stm32f4xx_hal_conf.h
│
├── Common/                 Bootloader 与 App 共享代码
│   ├── boot_params.c/h     A/B 槽仲裁参数（双副本 + sequence 投票）
│   └── crc32.c/h           IEEE 802.3 CRC32（OTA 校验 + 槽位完整性）
│
├── Drivers/                STM32 HAL + CMSIS（vendor 拉来不动）
│
├── cmake/
│   ├── arm-none-eabi-gcc.cmake    工具链定义（Cortex-M4F + newlib-nano）
│   └── ld/                链接脚本
│       ├── STM32F407_BOOTLOADER.ld
│       ├── STM32F407_APP_A.ld
│       └── STM32F407_APP_B.ld
│
├── tools/                  Python 辅助
│   ├── make_params.py             生成 boot params .bin
│   ├── ota_push.py                通过 ESP32 推 OTA 包到主板
│   └── serial_monitor.py          串口监听 → tools/serial_log.txt
│
├── build/                  构建输出（gitignore）
│   ├── bootloader/bootloader/     bootloader.{elf,hex,bin,map}
│   ├── app-a/app/                 upboard_A.{elf,hex,bin,map} + params_A.bin
│   └── app-b/app/                 upboard_B.{elf,hex,bin,map} + params_B.bin
│
├── knowledge-base/         跨项目可复用经验（hal-patterns / known-bugs / debug-recipes / rtos-patterns）
├── .context/               本项目状态（project / hardware / decisions log）
├── .skills/                AI 协作脚本（skill-0 ~ skill-6）
├── .claude/ .vscode/       工具配置
└── CLAUDE.md               AI 协作约定（自动加载）
```

---

## 快速开始

### 工具链

| 工具 | 版本 | 来源 |
|---|---|---|
| CMake | 3.31.10 | vcpkg |
| Ninja | 1.13.1 | vcpkg |
| arm-none-eabi-gcc | 10.3.1 | chocolatey |
| OpenOCD | 0.12.0 | chocolatey |
| ST-LINK_CLI | — | STM32 ST-LINK Utility |

`PATH` 拼装见 `CLAUDE.md` "工具链约定" 段。

### 构建

```bash
# 增量编译（首次会自动 configure）
cmake --build --preset bootloader
cmake --build --preset app-a
cmake --build --preset app-b

# 全部三个一起编（共享代码改动后）
for p in bootloader app-a app-b; do cmake --build --preset $p; done

# 干净配置
cmake --preset <name>
```

Debug 版本（`-Og -g3`）：`bootloader-debug` / `app-a-debug` / `app-b-debug`。

### 烧录（首次 bring-up）

```bash
STLINK="/c/Program Files (x86)/STMicroelectronics/STM32 ST-LINK Utility/ST-LINK Utility/ST-LINK_CLI.exe"

"$STLINK" -c SWD UR -ME                                                                             # 整片擦
"$STLINK" -c SWD UR -P build/bootloader/bootloader/bootloader.hex -V after_programming              # bootloader
"$STLINK" -c SWD UR -P build/app-a/app/params_A.bin 0x08008000 -V after_programming                 # 槽位参数 → A
"$STLINK" -c SWD UR -P build/app-a/app/upboard_A.hex -V after_programming -Rst                      # App A，运行
```

后续单 image 烧录见 `CLAUDE.md`。

### 调试

VSCode F5：`.vscode/launch.json` 配了 4 套 Cortex-Debug —— Bootloader / App A / App B / Attach。

---

## Flash 布局

| 地址 | 大小 | 内容 |
|---|---|---|
| `0x08000000` | 32 KB  | Bootloader |
| `0x08008000` | 16 KB  | Boot 参数 A 副本 |
| `0x0800C000` | 16 KB  | Boot 参数 B 副本（双副本 + sequence 投票，详见 `Common/boot_params.c`） |
| `0x08020000` | 128 KB | App Slot A |
| `0x08040000` | 128 KB | App Slot B |

OTA 通过：BLE → ESP32-C3 → USART2 帧协议 → bootloader 仲裁 → 4 场景回滚（正常 / boot_count / CRC / 稳态）。

---

## 关键约定

- **USART2 (PD5/PD6) 归 ESP32 协议使用** — bootloader / 工具代码禁打调试明文，违反会污染 ACK 流
- **Core/ 是 CubeMX 自动生成区** — 重新生成 `.ioc` 时会被覆盖，业务代码请放在 `app/Src` `app/Inc`
- **printf 在 newlib-nano 下默认无输出** — 用 `snprintf + uart_send_string`，详见 `knowledge-base/hal-patterns.md`
- **决策记录追加到 `.context/decisions.log.md`**，不修改历史

---

## 文档导航

| 文件 | 给谁看 | 内容 |
|---|---|---|
| `README.md` | 人 | 本文档：工程概览 + 构建烧录 |
| `CLAUDE.md` | AI | 协作约定 + 工具链命令 + skill 索引 |
| `.context/project.context.md` | 双方 | 模块状态 · TODO · 资源用量 |
| `.context/hardware.context.md` | 双方 | 引脚表 · 时钟树 · 外设配置 |
| `.context/decisions.log.md` | 双方 | 架构决策（追加） |
| `knowledge-base/*.md` | 双方 | 跨项目经验库 |
| `.skills/skill-*.md` | AI | 工作流脚本（冷启动 / 需求 / 硬件 / 基线 / 拆解 / 迭代 / 集成） |
