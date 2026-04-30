# 决策记录

> 只追加，不修改历史。每条记录对应一个具体决策。
> 历史决策从 OTA_PLAN.md / 提交记录 / auto-memory 回填，2026-04-18 一次性补齐。

---

## [2026-04-17] OTA Phase 2：跳转前清 NVIC 并 `__enable_irq()`

**背景**：Bootloader 跳到 App 后，App 第一次 `HAL_Delay` 死锁。
**决策**：`jump_to_app()` 在 `__set_MSP` 之前清 NVIC ISER/ICPR 并显式 `__enable_irq()`。
**原因**：Bootloader 退出时 PRIMASK=1，App 不会再开中断；HAL 依赖 SysTick 中断递增 tick，没中断 → `HAL_Delay` 永久阻塞。
**排除方案**：在 App 入口主动 `__enable_irq()` — 太晚，SystemInit 之前的代码也可能阻塞。

---

## [2026-04-17] OTA Phase 3：Bootloader 不碰 USART2，槽位用 LED 闪烁次数指示

**背景**：USART2 是 ESP32 的透传通道，Bootloader 在上面打明文会污染 BLE 链路。
**决策**：Bootloader 完全静默，LED1 闪 1 次=fallback / 2 次=A / 3 次=B。
**原因**：USART2 归 ESP32 使用（auto-memory feedback）。
**排除方案**：开新串口 — 引脚不够，且违反"工具代码禁动 USART2"的强约束。

---

## [2026-04-17] OTA Phase 4：用 `RTC->BKP0R` 而非 VTOR 标记当前槽位

**背景**：App 链接在 0x08020000，复制到 Slot B 后向量表里 Reset_Handler 仍是 0x0802xxxx，CPU 实际跑在 A；SystemInit 又把 VTOR 写回 0x08020000。
**决策**：Bootloader 跳转前启用 PWR + DBP 后写 `BKP0R = 0xA0A0A0A0`(A) / `0xB0B0B0B0`(B)；App 读 BKP0R 判槽。
**原因**：备份域不被软复位清除；不依赖镜像内容。
**排除方案**：改链接脚本让两份 bin 各自跳过 SystemInit 的 VTOR 设定 — 侵入性大且每个槽要维护一份。

---

## [2026-04-17] OTA Phase 5：双工程双 hex（Slot A / Slot B 独立链接）

**背景**：单一 bin 写到 B 槽时向量表指回 A 区，无法直接运行。
**决策**：克隆 `upboard.uvprojx` → `upboard_B.uvprojx`，改 OCR_RVCT4 起始地址 0x08040000 + Define `VECT_TAB_OFFSET=0x00040000U`。产物是两份内容相同但地址不同的 bin。
**原因**：保持 SystemInit 不变；Bootloader 跳哪个槽就跑哪份固件。
**排除方案**：运行时重定位向量表 — 增加复杂度且依赖 BL 协助。

---

## [2026-04-18] OTA Phase 6：OTA 期间抑制自发 STATUS 帧

**背景**：500ms 主循环的 `EspComm_SendStatus`（`A0` len=8）会和 OTA ACK 交错，PC 端解帧错位。
**决策**：主循环判 `OtaProto_IsBusy()`，OTA 进行中跳过 STATUS。
**原因**：共享 UART 上的 stop-and-wait 不能被异步发送污染。
**排除方案**：再加一条 OTA 专用 UART — 没有引脚预算且违反"USART2 归 ESP32"。
**衍生约束**：心跳/告警等任何自发帧都受同一规则约束（auto-memory feedback）。

---

## [2026-04-18] OTA Phase 6：帧格式 ACK 与请求同 CMD，靠 LEN 区分

**背景**：把 ACK 单独定义新 CMD 会把命令空间拆得乱。
**决策**：`AA | CMD | LEN | PAYLOAD | XOR`；ACK = `AA | CMD | 01 | STATUS | XOR`。
**原因**：复用 CMD 字段简化解析；LEN=1 + PAYLOAD=STATUS 是稳定的 ACK 指纹。
**排除方案**：分配独立 ACK CMD（如 0x80~0xFF）— 维护两套表，IDE 跳转难。

---

## [2026-04-18] OTA Phase 6：CRC32 用 IEEE 802.3（zlib 等价），bin 计算前补 0xFF 到 4B 对齐

**背景**：Flash 写入按 word（4B），bin 文件长度未必是 4B 倍数。
**决策**：`tools/ota_push.py` 在算 CRC 前先把 bin 末尾补 0xFF 到 4 字节对齐；`Common/crc32.c` 用 IEEE 802.3 多项式无表实现。
**原因**：与 zlib.crc32 完全一致，PC 侧可直接用 Python 标准库验证。
**排除方案**：CRC16 — 容错弱；自定义 CRC — 难调试。

---

## [2026-04-18] OTA Phase 7：boot_params 双副本 + sequence 仲裁

**背景**：Phase 3 用 magic + reserved，没法判定哪一份副本是最新写入。
**决策**：`reserved[4]` → `sequence + reserved[3]`；`BootParams_Read` 比较两副本 sequence 取大者，仅当两个都通过 CRC 时仲裁。
**原因**：Flash 擦写中途掉电，需要"上一次成功写入"的明确语义。
**排除方案**：单副本 — 写入中途掉电直接砖。

---

## [2026-04-18] OTA Phase 7：IWDG 起在 Bootloader（/256, RLR=500 ≈ 4s），App 长任务内部喂狗

**背景**：IWDG 一旦起就关不掉。Buzzer 大疆开机音 ≈ 10s，超过 4s 喂狗窗口。
**决策**：Bootloader 末尾启 IWDG；App 主循环首行 `IWDG->KR=0xAAAAU`；`Buzzer_PlayHajimi` / `Buzzer_PlayStartup` 在内部音符循环里主动喂狗。
**原因**：阻塞型长任务必须自己喂狗，否则 init 阶段就被复位（现象：蜂鸣器一直响 = 死循环 reset）。
**排除方案**：把开机音裁到 4s 内 — 牺牲产品体验。
**衍生约束**：以后任何 >2s 的阻塞函数都必须内部喂狗（auto-memory feedback）。

---

## [2026-04-18] OTA Phase 7：MarkBootOk 触发条件用 `HAL_GetTick() >= 10000` 单次调用

**背景**：开机后稳定运行 10s 即认定固件 OK，清 `pending_update` / `boot_count`。
**决策**：主循环里 `HAL_GetTick() >= 10000` 一次性调 `Ota_MarkBootOk()`，内部脏才写 flash。
**原因**：阈值需远大于业务最长启动耗时（开机音 ≈ 10s）；脏检测避免每次循环都擦 sector。
**排除方案**：复杂的"用户确认"协议 — 摩托车场景没有 UI 让人确认。

---

## [2026-04-18] 接入 .skills / .context / knowledge-base 工作流

**背景**：项目已完成 OTA Phase 7，转入运维和小步迭代阶段。原有 PLAN.md / DEV_DOC.md / OTA_PLAN.md 是任务日志，缺少结构化的"会话冷启动"上下文。
**决策**：引入 `AGENTS.md` + `.skills/skill-0~6` + `.context/{project,hardware,decisions}` + `knowledge-base/`，所有新会话先走 skill-0 冷启动握手。
**原因**：每次新会话都要重新解释项目背景代价高；结构化上下文让任意 agent / 终端能在 30 秒内热接入。
**排除方案**：只靠 auto-memory（`~/.claude/.../memory/`）— 不在仓库里，换机器/换 IDE 就丢；CLAUDE.md 单文件 — 信息塞不下，且没法按 skill 切片。

---

## [2026-04-20] 目录规范化：业务代码 Core/ → app/

**背景**：CubeMX-Keil 时代生成的布局把所有 .c/.h 都堆在 `Core/Src` `Core/Inc`，业务模块和 CubeMX 自动生成代码混杂。CMake 改造后 `app/` 是空壳（只有 CMakeLists），目录名与内容不匹配。

**决策**：
- 业务模块（buzzer / esp_comm / fan_ctrl / compressor_ctrl / sensor_acq / ina226 / power_ctrl / ota）从 `Core/{Src,Inc}` 迁至 `app/{Src,Inc}`
- `Core/` 只留 CubeMX 自动生成的 wiring：main / gpio / i2c / usart / stm32f4xx_it / _hal_msp / _hal_conf / system_stm32f4xx
- `Common/` 保持不变（bootloader/app 共享：boot_params / crc32）
- `app/CMakeLists.txt` 同步更新：APP_SRC_DIR + 加 app/Inc 到 include
- 同步删除 Keil 残留：`.mxproject`、根目录 `params_A.bin`，`MDK-ARM/` 待 IDE 释放后自然消失
- 新增根 `README.md`（给人看）+ `.gitignore`

**原因**：目录名 = 内容职责，CubeMX 重新生成时不会触碰 app/ 下的手写代码。bootloader 不引用业务模块，无需改动。

**排除方案**：
- 保留旧布局：违反"目录名表达内容"原则，新人接手要花时间分辨哪些是 CubeMX 生成的
- 业务代码就地保留 + 给 Core/ 改名：CubeMX 重新生成代码时仍会写到 Core/，无法解决

**验证**：三 preset 全编 0 error 0 warning，bootloader 6008 B / app 17880 B Flash 用量与重构前完全一致。

---

## [2026-04-28] 目标芯片修正：STM32F407VGT6 → STM32F407VET6（Flash 1024 KB → 512 KB）

**背景**：迁移到 macOS + OpenOCD 烧录链后第一次给新板子做 bring-up，OpenOCD 报 `device id = 0x10076413` + `flash size = 512 KiB`，`mdh 0x1FFF7A22 = 0x0200`，与原文档 STM32F407VG**T6**（1024 KB）不符。芯片丝印确认为 STM32F407VE**T6**。

**决策**：项目目标芯片更正为 **STM32F407VET6 / 512 KB**。同步更新 `hardware.context.md`、`project.context.md`、`README.md`、`.vscode/launch.json`（Cortex-Debug `device` 字段）。链接脚本注释里残留的 "STM32F407VG" 不动（地址正确，纯注释，无功能影响）。

**原因**：
- VE 与 VG 仅 Flash 容量差异（512 vs 1024 KB），外设 / RAM / 封装 / 引脚一致 → 现有代码、链接脚本地址段、HAL 配置全部不需要改动
- 当前 OTA 布局仅占 0x08000000~0x0805FFFF = 384 KB，512 KB 容下绰绰有余（剩 128 KB 可作未来日志/参数备份扩展）
- 错误的容量标注会误导未来 agent 在末端 sector 规划新功能时越界

**验证**：本次完整 bring-up（mass erase → bootloader → params_A @ 0x08008000 → App A @ 0x08020000）全部 `Verified OK`；mdw 回读 BL 入口 / params A magic / App A 入口全部正确，板子复位后 LED2 槽位指示由用户肉眼确认。

**排除方案**：
- 保留旧文档不动 — 错误的"1024 KB"假设会污染未来规划（如 OTA 日志扩到末端 sector）
- 同时维护 VG 和 VE 两套配置 — 项目只有一种板子，无收益

**衍生约束**：未来若要使用 0x08060000 以上地址（如末端 sector 做日志/工厂数据），必须先确认实际板子容量；不可再假设 1024 KB。

---

## [2026-04-30] MCF8329A 驱动起转:WAKE 引脚走线断 + Motor Studio 完整配置

**背景**:阶段 6 MCF8329A I2C 通信打通后,芯片始终卡在 ALGORITHM_STATE = 0 (MOTOR_IDLE),三相输出无 PWM。所有 STM32 端尝试 (SPEED_OVERRIDE / FORCE_ALIGN / MPET / 全 Motor Studio 影子寄存器灌入) 都无法让芯片离开 IDLE,但所有故障寄存器全 0,SYS_ENABLE_FLAG = 1。

**根因**:STM32 PD0 (SPEED/WAKE) 到 MCF8329A SPEED/WAKE 引脚的 PCB 走线断开。STM32 端 PD0 拉高 3.3V,但芯片本体 WAKE 引脚实测 0V。芯片内部 1MΩ 下拉把 WAKE 钉死,芯片处于 standby 模式,I2C 寄存器仍可访问 (305+ 次读写零错误),但栅驱被禁用,所以输出永远 0V。PIN_CONFIG 配置为 I2C SPEED_MODE,WAKE 引脚仅作 sleep/wake 阀门,需要 > 0.65 × AVDD = 2.15V 才能 active。

**决策**:
1. WAKE 走线问题硬件修复 (飞线 / 重焊)
2. 软件保留 Motor Studio "BLDC_Pump_3A_200Hz_MCF8329A_v1" 的 24 个 EEPROM 影子寄存器配置 (在 `app/Src/mcf8329a.c::MCF8329A_LoadMinimumConfig()`)
3. 启动序列:I2C SWRST 解锁 → 强制 WAKE 边沿 LOW→HIGH → ClearFault → KickWatchdog → 灌全配置 → 写 ALGO_DEBUG1 spin override
4. 主循环 200ms 周期 tickle WATCHDOG_TICKLE (ALGO_CTRL1 bit10) + 重写 spin override + 读 status

**原因**:
- 走线断这种硬件 bug 无法通过软件 workaround
- 软件层在硬件可用前就准备好了完整的控制路径,修复后立刻生效 (motor 当场起转)

**目标电机**:生利达 ZW50-3.3-24 全封闭旋转式直流变频压缩机 (8 极, 3000~4500 RPM, 24V 3.3A, 0.244Ω L-L, Ld=Lq=0.076mH L-L, 8 Apk 退磁极限)

**Pump_3A_200Hz 配置 vs ZW50 实际差异**:
- 最高转速:配置 200Hz (3000 RPM) vs 实际需 300Hz (4500 RPM)
- 推荐升速:配置激进 vs 实际需 60 rpm/s (4 Hz/s 电气)
- 现状能转,但不优化。生产前需重新 Motor Studio 配置导出 JSON,替换 LoadMinimumConfig 内的常量表。

**排除方案**:
- 反复尝试不同的 ALGO_DEBUG1 / ALGO_DEBUG2 组合:都失败,因为芯片 standby 模式根本不响应业务命令
- 怀疑 EEPROM 烧坏 / chip 损坏:误判,实际是 I/O 走线
- 接受电机不转 + 用 MPET 应付:走错方向,MPET 期间的 10V 是 DC 注入测参,不是真驱动

**验证**:WAKE 走线修复后,同样的固件让 motor 立刻起转;所有诊断全局 (g_mcf_*) 显示 algo_state 离开 IDLE,VOLT_MAG 增长,三相 PWM 切换。

**衍生约束**:任何"芯片 I2C 通但功能不工作"先查关键控制信号是不是真的到了芯片本体引脚 (用万用表对芯片引脚直接测,不要相信 STM32 侧的 GPIO ODR)。

---

## 2026-04-30 — MCF8329A 切到出厂模式 + I2C 地址自动扫描

**决策**:
1. STM32 启动时**不再调** `MCF8329A_LoadMinimumConfig()`,放弃精确 FOC 路线,改用 EEPROM 出厂/调通配置 + `SpinDuty(SPEED_OVERRIDE)` 直接驱动
2. `MCF8329A_Init()` 启动时自动扫描 I2C3 总线,**不依赖固定的 0x01 地址**
3. 移除 `g_mcf_i2c_disable` 的 BKP2R 持久化,启动总是清零让 STM32 接管(运行时仍可 SWD 设 1 让出总线)
4. `g_mcf_spin_duty` 默认改 `0x4000 (50%)` → `0x7FFF (100%)`

**原因**:
- 反复 SmartTune 调参始终触发 HW_LOCK_LIMIT(根因疑似 Shunt / Phase R/L 单位 / BEMF 不准),浪费时间
- Motor Studio 的"I2C Speed Command Percentage"滑块用的是出厂默认配置 + 通用启动状态机,容差 ±50% 都能转 — 直接用同款模式即可
- Motor Studio 写 EEPROM 把芯片地址从 target 0x01 改到了 **target 0x5A (HAL 0xB4)**,固定 0x02 地址永远 NACK
- 硬编码地址会随 EEPROM 任意一次写动作失效,自动扫描是唯一鲁棒方案

**实测验证(2026-04-30, ST-Link + OpenOCD)**:
- `g_mcf_scan_addr = 0xB4` ← 自动扫到 0x5A target
- `g_mcf_w_ok = 4, g_mcf_w_err = 0` ← I2C 通信 100% 成功
- `g_mcf_spin_duty = 0x7FFF` ← 100% duty 已写
- `g_mcf_ctrl_fault = 0, g_mcf_gate_fault = 0` ← 零故障
- **物理实测电机全速转动**

**衍生约束**:
- 任何对 MCF8329A 的代码修改,**不要假设 I2C 地址固定**,通过 `dev->addr` 间接访问
- 想切回精确 FOC,在 main.c 启动序列里恢复一行 `LoadMinimumConfig()` 即可,函数代码留着
- Motor Studio 调试若要重新接管,运行时 SWD 写 `g_mcf_i2c_disable = 1`(不再跨复位保留)

