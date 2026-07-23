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
| 风扇控制 fan_ctrl | 🚧 | 协议已修正为唯一 `SET_GEAR(0x20)` 的on位联动PC10；on=1风扇供电，on=0关闭，待构建/烧录 | 需联调S3开关并现场确认风扇运行 |
| 压缩机控制 compressor_ctrl | 🚧 | PWM已从PA15迁移到PA0/TIM2_CH1 AF1，保持5kHz active-HIGH；Bootloader与App A已烧录校验，SWD确认PA0 AF1、PSC=83、ARR=199、CCR1=0 | 待示波器确认PA0波形、档位及机械转向 |
| NTC 采集 sensor_acq | ✅ | ADC1+DMA2 8 通道循环扫描 | 仍为原始 ADC 值，未转温度（L3） |
| 功率监测 INA226×3 | ✅ | I2C1/2/3 各一路，500ms 轮询 | cal_val 未实测校准（L4） |
| 蜂鸣器 buzzer | ✅ | 原有PB14/TIM1_CH2N代码保持不变，开机大疆音与按键提示音均保留 | — |
| 充电/水泵控制 | 🚧 | PC11 100Hz软件PWM支持0–100%；云端60%→0%闭环已实机验证，SWD确认duty/enable/ODR与命令一致，telemetry同步回传 | 仍需联调S3开关，并用示波器/流量计标定占空比与实际流量 |
| 保护逻辑 protection | ⬜ | — | 待开发，依赖 NTC→℃ 转换 |
| Bootloader（OTA Phase 1~5） | ✅ | 0x08000000 独立工程，A/B 槽跳转 + LED 指示 | — |
| OTA 协议（Phase 6） | ✅ | BLE→ESP32-C3→USART2 端到端通过（2026-04-18） | — |
| OTA 回滚（Phase 7） | ✅ | 4 场景验证：正常/boot_count/CRC/稳态 | 双向 OTA 联调 + CI 待做 |
| EC801E 物联网 iot_ctrl | ✅ | App A 固化自动初始化/重连/5s 周期上报；MQTT连接后订阅 `upboard/{sn}/command/pump`，UART ISR解析命令并由主循环执行；2026-07-23实机收到60%和0%命令，序号递增、非法计数0，telemetry均在5s周期内回传 | GPS 实板有星定位待验证；普通用户绑定后续做 |
| MQTT 管理后台 V1 | ✅ | `hk-newapi` Docker Compose：Mosquitto 1883 + PostgreSQL + FastAPI 18080；遥测入库/latest API已验证；新增管理员水泵控制API，以QoS1向设备专属主题下发0–100% duty；生产API→MQTT→设备→telemetry闭环已验证 | 仅管理员查看全部设备；普通用户绑定、TLS/HTTPS后续做 |
| THERMORIDE Premium 遥测前端 | ✅ | `server/upboard_iot/frontend`：React 19 + TypeScript strict + Vite + Tailwind 4 + shadcn/ui 风格组件 + ECharts + Leaflet + Zustand + React Query；底部新增水泵流量控制面板，支持滑杆/预设/停止及设备回传确认；线上60%与0%实机闭环、401/422、截图及浏览器0 error 0 warning已验证；已部署 `hk-newapi:18080` | 生产暂为 HTTP，TLS/HTTPS仍待配置；NTC温度和水泵实际流量仍依赖硬件标定 |
| THERMORIDE Figma 可编辑设计 | ✅ | Figwright 已连接 Figma 文件《THERMORIDE 实时遥测平台》并整理为 `01 Screens`、`02 Components`、`03 Foundations`：含登录页、Dashboard 首屏、地图/24V 历史滚动页、执行器/NTC 上半区、NTC 全八路下半滚动页、组件库及颜色/字体/间距/圆角/阴影规范；已按 1280×720 网页实测尺寸校正登录布局、地图控制、曲线坐标轴、执行器卡片、NTC 真实行高及 System Health 紧凑响应式布局，Dashboard 与 Component Library 两处均截图验证无叠层，全部文本为 Geist；登录按钮已配置原型跳转到 Dashboard | 当前文件为本地/草稿连接，`fileKey=null`；如需分享链接需在 Figma 中保存到云端 |

状态说明：⬜ 未开始 · 🚧 开发中 · ✅ 已验证

---

## 资源用量

| 时间点 | Flash (App) | RAM | 备注 |
|--------|-------------|-----|------|
| 基线（CubeMX 空工程） | — | — | 项目早于 skill 系统建立，无基线 |
| 当前 App（2026-04-18） | 16.86 KB / 128 KB（Slot 容量，13%） | 2.51 KB / 192 KB | RO=16600+660，RW=156，ZI=2412 |
| 当前 Bootloader | 5.26 KB / 128 KB（BL 区） | — | 0x08000000~0x0801FFFF |
| 当前 App（2026-06-17，EC801E 探针） | 24.67 KB / 128 KB（Slot 容量，19.27%） | 4.77 KB / 192 KB | text=25136, data=128, bss=4760 |
| 当前 App（2026-06-17，EC801E MQTT + 采样板 GPS 上报） | 31.77 KB / 128 KB（Slot 容量，24.82%） | 6.00 KB / 128 KB（SRAM1，4.69%） | text=32296, data=232, bss=5920 |
| 当前 App（2026-07-11，压缩机开机自动启动） | 31.89 KB / 128 KB（Slot 容量，24.92%） | 6.00 KB / 128 KB（SRAM1，4.69%） | text=32420, data=232, bss=5920；App A 已烧录验证自动发起启动 |
| 当前 App（2026-07-11，MCF8329A TI 官方推荐参数） | 32.37 KB / 128 KB（Slot 容量，25.29%） | 6.01 KB / 128 KB（SRAM1，4.69%） | text=32900, data=236, bss=5924；App A 已烧录，shadow 读回 24/24 一致 |
| 当前 App（2026-07-13，水泵 30% 软 PWM） | 33.11 KB / 128 KB（Slot 容量，25.86%） | 6.02 KB / 128 KB（SRAM1，4.70%） | text=33652, data=240, bss=5928；App A/B 构建通过，未烧录，待测 PC11 波形和实际转速 |
| 当前 App（2026-07-23，水泵 30% 烧录验证） | 33.11 KB / 128 KB（Slot 容量，25.86%） | 6.02 KB / 128 KB（SRAM1，4.70%） | App A `Programming Finished` + `Verified OK`；运行态 duty=30、startup=0、TIM7 CEN/UIE=1，软件 PWM 输出状态发生切换 |
| 当前 App（2026-07-23，V7 压缩机 PWM 接口） | 27.31 KB / 128 KB（Slot 容量，21.33%） | 5.63 KB / 128 KB（SRAM1，4.39%） | text=27788, data=168, bss=5600；App A/B 构建通过，旧 MCF8329A 运行代码未进入 ELF；尚未烧录 |
| 当前 App（2026-07-23，V7 压缩机 5s 软启动） | 27.50 KB / 128 KB（Slot 容量，21.48%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=27984, data=168, bss=5608；bootloader + App A `Programming Finished`/`Verified OK`；SWD 实测斜坡 109ms=2%、2116ms=42%、5000ms=100% |
| 当前 App（2026-07-23，V7 压缩机默认反转） | 27.50 KB / 128 KB（Slot 容量，21.48%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=27984, data=168, bss=5608；App A `Programming Finished`/`Verified OK`；SWD 验证 reverse=1、PC9 LOW、PA8 HIGH、5000ms/100% |
| 当前 App（2026-07-23，风扇 PC10 使能 + 压缩机立即满 PWM） | 27.52 KB / 128 KB（Slot 容量，21.50%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=28000, data=168, bss=5608；App A `Programming Finished`/`Verified OK`；SWD 验证 PC10 HIGH、duty=100%、ramp_active=0 |
| 当前 App（2026-07-23，压缩机 PWM 5kHz） | 27.52 KB / 128 KB（Slot 容量，21.50%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=28000, data=168, bss=5608；App A `Programming Finished`/`Verified OK`；SWD 验证 PSC=83、ARR=199、CCR1=200 |
| 当前 App（2026-07-23，S3独立三执行器控制） | 27.57 KB / 128 KB（Slot 容量，21.53%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=28044, data=168, bss=5608；App A/B 构建通过；开机全关，0x20压缩机档位/开关、0x21风扇开关、0x22水泵开关，尚未烧录 |
| 当前 App（2026-07-23，S3独立控制 + 水泵30% PWM） | 27.71 KB / 128 KB（Slot 容量，21.65%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=28204, data=168, bss=5608；App A `Programming Finished`/`Verified OK`；SWD验证开机压缩机/风扇/水泵全关，水泵duty预设30%但未输出 |
| 当前 App（2026-07-23，压缩机PWM迁移PA0） | 27.61 KB / 128 KB（Slot容量，21.57%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=28096, data=168, bss=5608；Bootloader/App A烧录Program+Verify通过；VTOR=0x08020000，PA0 AF1，PA15 analog，TIM2 PSC=83/ARR=199/CCR1=0 |
| 当前 App（2026-07-23，EC801E 5s 上报） | 27.61 KB / 128 KB（Slot容量，21.57%） | 5.63 KB / 128 KB（SRAM1，4.40%） | text=28096, data=168, bss=5608；App A/B 构建通过，App A 烧录 Verified OK；16s 内成功上报 3 包、失败 0 包 |
| 当前 App（2026-07-23，云端水泵调速） | 28.91 KB / 128 KB（Slot容量，22.05%） | 5.81 KB / 128 KB（SRAM1，4.43%） | text=28736, data=172, bss=5636；App A/B构建通过，App A烧录Verified OK；60%→0%云端闭环实机验证 |
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
- [ ] S3 三执行器协议联调：0x20压缩机1–10档/开关、0x21风扇开关、0x22水泵开关；确认开机三者全关
- [ ] THERMORIDE 生产 HTTPS/TLS：反向代理、Secure Cookie、证书续期与 MQTT TLS
- [ ] V7 压缩机物理波形验证：示波器测 PA0=5kHz active-HIGH；实测 PC9 HIGH正转/LOW反转、PA8 LOW停/HIGH运行；确认 3.3V 输入兼容性
- [ ] 压缩机档位实测：1–10档对应 PA0 10%–100%，运行中重复ON/换挡不得产生STOP脉冲

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
| L7 | EC801E POW/NET 指示灯未亮 | 串口已收到 `RDY` 和 `OK`，且已完成蜂窝 PDP + TCP 到 `38.76.206.42:22` 冒烟；灯不作为唯一上电/联网判断；若需定位灯，测 EC801E VDD_EXT/NETLIGHT/LED 供电路径 |
| L8 | MCF8329A `GATE_DRIVER_FAULT_STATUS=0x82000000` | 已解码为 GVDD_UV；测 U21 pin8，正常 11.5~15.5V，检查 C88 10uF 与 C87 470nF |
| L9 | TI Table 8-1 通用参数空载仅勉强起转 | 说明控制链路可工作但电机模型/启动参数不匹配；带压缩机负载前需 MPET 获取本机 Rs/L/Ke，或回退已验证参数 |
| L10（云端闭环已验证，待S3/流量标定） | 水泵仅有两线供电开关，采用PC11供电软PWM调速 | App A已烧录；云端60%与0%命令的duty/enable/ODR及telemetry均已验证；仍需S3联调，并用示波器测100Hz占空比、流量计建立duty→流量曲线 |
| L11（已解决 2026-07-23） | J-Link 曾可枚举但 SWD 报 `cannot read IDR` | 本次已读到 DPIDR=0x2BA01477、DBGMCU_ID=0x10076413，并完成 App A program/verify |
| L12（PA0迁移已烧录，待示波器） | V7压缩机接口现为PA0 PWM、PC9 DIR、PA8 STOP | Bootloader/App A已烧录；SWD确认PA0 AF1、TIM2 5kHz配置和默认CCR1=0，仍需示波器测实际引脚波形 |
| L13（代码已烧录，待S3联调） | C3/S3重复发送SET_GEAR ON会反复重启压缩机 | 当前代码仅OFF→ON调用Start，运行中重复ON或换挡只调用SetDuty；待S3命令验证 |
| L14（2026-07-23 网表错误已纠正） | 2026-04-28 网表错误地把 PA15 与 PB14 列在同一 `BEEP_CTRL`，用户确认实板未连接 | MCU暂停后 PA15 输入下拉，IDR bit15=0；对仍显示3.3V的测试点，断电测其到U9.77的电阻，确认是否测错网络或存在板级改线 |

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
