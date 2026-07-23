# 决策记录

> 只追加，不修改历史。每条记录对应一个具体决策。

## [2026-07-22] THERMORIDE 网页以 Figwright 原生可编辑图层重建

**背景**：需要把已部署的 THERMORIDE 遥测网页放入 Figma，并保证设计元素可继续编辑；当前 Figwright 已连接到空白 Figma Design 文件，但该文件没有现成变量、样式或组件，且未暴露可用的云端 fileKey。
**决策**：以线上实际桌面视口 1280×720 为视觉基准，通过 Figwright 在当前文件中创建原生 Frame、Auto Layout、Text 与 SVG Vector 图层；按页面外壳、导航、设备列表、摘要卡片和遥测卡片分层命名，并对重复模式采用组件化/一致结构。
**原因**：该方案直接写入当前已连接的 Figma 文件，不依赖整页截图或云端捕获接口；文本、颜色、圆角、图标和容器均保持可编辑，并可从 React/Tailwind 源码追溯准确值。
**排除方案**：整张截图导入——无法编辑；依赖 `generate_figma_design` 云端捕获——当前 Figwright 文件 `fileKey=null`，无法可靠指定目标文件；等待用户另建文件——会中断当前已建立的插件连接。

---

## [2026-07-22] THERMORIDE Figma 交付采用 Screens / Components / Foundations 三页结构

**背景**：首轮 Figma 仅重建了 Dashboard 的 1280×720 首屏，无法完整表达登录、滚动区地图与历史曲线、执行器、8 路 NTC，以及产品设计规范。用户要求完整加入，并按正规 Figma 文件的方式展示。

**决策**：将文件整理为 `01 Screens`、`02 Components`、`03 Foundations`。Screens 使用四个独立 1280×720 桌面画板展示登录、Dashboard 首屏和两个滚动位置；Components 收纳可复用摘要卡、选中态、系统健康、执行器行和 NTC 行；Foundations 建立生产配色、Geist 字体、8px 间距、圆角和卡片阴影的本地样式及可视规范。登录按钮通过原型交互跳转到 Dashboard。

**原因**：多画板能保持真实桌面视口和滚动语义，组件与基础规范页让设计可复用、可审阅、可继续编辑；所有内容仍由原生 Frame、Text、Shape 和 SVG Vector 构成，可追溯到 React/Tailwind 源码。

**排除方案**：单个超长画板——难以按真实视口评审；把整站压成一张截图——不可编辑；只补缺失卡片但不建立组件与规范页——文件结构仍不符合可交付设计稿的常规组织方式。

---

## [2026-07-22] NTC 使用第三个滚动状态保持网页真实行高

**背景**：对照 1280×720 实际网页后发现，执行器卡片高度约 159px，NTC 面板完整高度约 693px；把 8 路 NTC 压缩到单个 720px 画板虽然能一次看全，但与真实网页的行高、图标、校准信息、量程滑轨和滚动行为不一致。

**决策**：`01 Screens` 保留 `Scroll 02` 展示执行器和 NTC 1–4 的真实滚动位置，并新增 `Scroll 03` 展示保持同一 693px 面板和真实 66px 行高的全部 8 路 NTC。登录页、地图和 24V 历史页同时按浏览器实测尺寸校正，地图瓦片内容仍以可编辑矢量道路层表达，交互控件、路线、状态浮层、坐标轴和阈值线单独分层。

**原因**：多个滚动状态与浏览器固定顶栏/侧栏的实际行为一致，同时能在 Figma 中完整审阅所有传感器；组件尺寸不再为“塞进一屏”而失真。

**排除方案**：继续压缩 NTC 行——与产品实现不一致且可读性降低；把整个页面截图导入——无法编辑；只保留前四路——交付内容不完整。
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

---

## 2026-05-11 — MCF8329A 电机方向控制位定位(仅记录,未改代码)

**结论**:DIR 引脚(PD3)被屏蔽的真正位置是 **PERI_CONFIG1 (0xAA) bits[20:19] DIR_INPUT**,**不是** `main.c:86-89` 注释里说的 PIN_CONFIG (0xA4)。原注释定位错了寄存器,需要纠正。

**Datasheet 出处**:SLLSFQ7(2023-11)§7.7.3.4 Table 7-39 PERI_CONFIG1 Register Field Descriptions。

**DIR_INPUT[20:19] 枚举**:
- `0h` = Hardware Pin DIR (用 PD3 引脚)
- `1h` = Override 强制 CW,相序 OUTA→OUTB→OUTC
- `2h` = Override 强制 CCW,相序 OUTA→OUTC→OUTB
- `3h` = Hardware Pin DIR (与 0h 等价)

**当前 EEPROM 值解码**(`mcf8329a.c:330` cfg 表):
- `PERI_CONFIG1 = 0x8BB57988`
- nibble 拆解:`8 B B 5 7 9 8 8` → bits[23:20]=B=`1011`, bits[19:16]=5=`0101`
- bits[20:19] = `10b = 2h` → **CCW override**
- 这与 `main.c:87-88` 观察一致("speed_fdbk 一直读出负值"、PD3 拉高低无效)

**永久反转方向的最小改动**(留作未来执行):
1. 把 `mcf8329a.c:330` 那行
   ```c
   { 0x0000AAU, 0x8BB57988U },   /* PERI_CONFIG1 */
   ```
   改成
   ```c
   { 0x0000AAU, 0x8BAD7988U },   /* PERI_CONFIG1 — DIR_INPUT: 2h CCW → 1h CW */
   ```
2. 推导:bit20 (1→0) 且 bit19 (0→1),两位翻转 → 偶校验不变,bit 31 PARITY 保持 `1` 无需重算
3. 验证位运算:`(0x8BB57988 & ~(1U<<20)) | (1U<<19) = 0x8BAD7988`
4. **前提**:`MCF8329A_LoadMinimumConfig()` 当前在 main.c 启动序列里没被调用(见上一条 2026-04-30 决策)。要让本改动生效,必须:
   - 临时恢复一次 `LoadMinimumConfig()` 调用把 shadow 刷下去,**且** 写 `ALGO_CTRL1 ← 0x8A500000`(EEPROM_WRT 命令)commit 到 EEPROM(datasheet §7.6.1.1 step 18),完成后再删除调用
   - 或者通过 Motor Studio 改 DIR_INPUT 字段后 Write to EEPROM
5. 改完之后 `main.c:86-89` 的注释也要同步修正:寄存器名换成 PERI_CONFIG1,事实改成"DIR_INPUT 由 CCW override 切到 CW override,PD3 仍然不生效因为 DIR_INPUT 仍是 override 模式"

**另两条路径**(本次未选,留档):
- 把 DIR_INPUT 改成 `0h`,让 PD3 重新生效 → `g_mcf_dir_cw` / `MCF8329A_SetDir()` 可运行时切方向
- 物理交换 U/V/W 中任意两相 → 不动固件

**衍生约束**:
- 任何改 EEPROM shadow 表的修改都要核对 bit 31 PARITY(偶校验);只翻偶数个 bit 时 PARITY 不变
- 引用方向相关行为时,锁定 PERI_CONFIG1[20:19],不要再把锅扣到 PIN_CONFIG

---

## 2026-05-11 — 换压缩机启动失败:MPET 是 sensorless FOC 的强前提,暴力堆参数无效

**结论**:换不同型号/不同负载的压缩机后,**必须用 Motor Studio 做一次 MPET 重新辨识 Rs/Ld/Lq/Ke**,然后 "Write to EEPROM" 把结果持久化。STM32 接管后不需要任何调参就能正常起转。**不要再试图通过 SWD 暴力堆 OL_ILIMIT / ALIGN_CURRENT / 关 LOCK 保护让它转 — 走不通,会浪费 1-2 小时**。

**症状链(完整复现)**:
1. 同型号压缩机有的能转有的不能 → 负载差异(气压/机械摩擦)
2. 100% duty + 原 EEPROM 配置 → 卡在 `algo_state=7` (OPEN_LOOP) 不报错
3. 降到 25% duty → 短暂到达 state 8 (CLOSED_LOOP) 然后 fault 掉,`last_ctrl=0x80500000` (`ABN_BEMF` bit22 + `MTR_LCK` bit20)
4. 加 `CLOSED_LOOP_DIS` 强制纯开环 → state 锁 7 不再 fault,但转子还是不动,只有滋滋声
5. 堆 `OL_ILIMIT=Fh(95%)` + `ALIGN_OR_SLOW_CURRENT=Fh(72%)` → 状态在 3↔0xE 循环,`last_ctrl=0x80080000` (`LOCK_LIMIT` bit19) — **电流上来了** 但保护打回
6. 关 `LOCK_ILIMIT_MODE=9h` + `MTR_LCK_MODE=9h` + `FORCE_ALIGN` 模式 → 持续灌 DC 大电流无故障,只有"规律的卡卡卡卡"声,**还是不转**
7. Motor Studio GUI 跑 MPET → 一次跑通,Write to EEPROM 持久化
8. STM32 接管(用新 EEPROM 参数)→ 开机 ~2.5 秒 align/ramp 后进入 `state=0x00200009` (CLOSED_LOOP) **稳定运行**

**根因 — 为什么暴力堆参数没用**:
- MCF8329A 是 sensorless FOC,**强依赖 Rs / Ld / Lq / Ke**(在 INT_ALGO_1/2 等寄存器里)做 BEMF 估算定位转子
- 旧 EEPROM 里的电机参数是旧压缩机辨识的,跟新负载完全不匹配
- 新转子在某个位置,但芯片基于错参数估算转子在别处 → 把大电流灌进**错误相位**
- 力矩 ∝ sin(电气角差),相位错就只有热效应(滋滋声),不产生有效转矩
- 关掉所有 LOCK 保护只是不报错,不解决"灌错相位"这个根因
- FORCE_ALIGN 是把转子拉到一个固定角度 DC 锁住,**不是用来转的** — 只能用来验证机械能不能动

**正确处方(下次直接执行,不要重复探索)**:
1. **暂时让出 I2C 总线给 Motor Studio**:SWD 写 `g_mcf_i2c_disable=1`,运行时 handler 自动把 BKP2R 写成 `0xD15AB1ED` + DeInit I2C3 + PA8/PC9 浮空。重启后 STM32 不抢总线
2. **Motor Studio GUI 操作**(用户在 GUI 里):
   - 连接 chip(独占 I2C 总线)
   - 点 **MPET / Motor Parameter Identification**(辨识 Rs/L/Ke)
   - 验证能起转 + 闭环稳定
   - 点 **Write to EEPROM**(把当前 RAM shadow 永久存储,300ms 完成)
   - 复位 chip 再次起转验证
3. **STM32 接管**:SWD 写 `RTC->BKP2R=0` + reset → boot 读到 BKP2R=0 → `g_mcf_i2c_disable=0` → 正常 init I2C3 + chip + SpinDuty(100%)

**关键代码(已就位)**:
- `Core/Src/main.c` 启动时根据 `BKP2R==0xD15AB1ED` 设 `g_mcf_i2c_disable`,disable 状态下完全跳过所有 I2C 操作 + 立即 DeInit I2C3 释放 PA8/PC9 浮空
- `g_mcf_cl4_boot_target=0` 默认(不再覆盖 EEPROM CL4,让 Motor Studio 的 MAX_SPEED 设置生效)
- 残留的 SWD set 通道(`motor_su1_set/motor_su2_set/fault_cfg1_set`)和 `g_mcf_force_open_loop` 留作调试逃生口,默认全 0 不影响正常路径

**衍生约束**:
- 任何新压缩机/新电机型号上线 → 第一步必须 MPET 重新辨识,**不要**直接拿 STM32 跑
- LoadMinimumConfig 表里的 EEPROM 镜像值仅供参考,**实际跑的是 chip EEPROM 里的值**(可能被 Motor Studio 改过) — 调试前先 SWD 读 readback 寄存器对照实际值
- Motor Studio 写 EEPROM 时可能会顺便改 chip I2C target 地址(我们见过从 0x5A 改回 0x01) → `MCF8329A_Init` 的自动 I2C 扫描机制必须保留
- 启动序列 ~2.5 秒到 CLOSED_LOOP 是正常的重负载 align/ramp 时间,不要误判为"启动失败"
- BKP2R 持久化依赖 VBAT(主板没电池 / VBAT 没接的情况下,断电就丢)— 实测当前主板 VBAT 能保住

**排除方案(走过的弯路,记下来别再走)**:
- 不同 duty 扫描(12.5% / 25% / 50% / 75% / 100%)— 都不行,因为参数错了 duty 无关
- 关 LOCK_ILIMIT_MODE / MTR_LCK_MODE — 只是让芯片不重启,不解决相位错误
- FORCE_ALIGN + 拉大 ALIGN_CURRENT — 转子卡在某个角度,不会转
- 改 MOTOR_STARTUP2 OL_ACC_A1 慢化 ramp — 当 BEMF 估算完全错时,慢/快 ramp 都不会让闭环 lock 上
- 怀疑硬件电流上限 / CSA_GAIN — 不是问题,Motor Studio 用同样硬件能跑就证明硬件 OK

---

## [2026-06-17] SamplerComm 兼容采样板 ASCII NTC 行

**背景**：J-Link 运行态读取 USART3 raw ring 证实综合采样板当前发送 ASCII 行，例如 `ACC:0.00,0.00,0.00 GYR:0.0,0.0,0.0 T:0.0 GPS:no_fix NTC:1891,10,9,10,2538,2540,2537,2537\r\n`，而主控只解析 `0xAA | CMD | LEN | PAYLOAD | XOR` 二进制帧，导致 `g_sampler_isr_cnt` 增长但 `g_sampler_frames_rx=0`、`s_st.stale=1`。
**决策**：主控 `SamplerComm` 保留原二进制帧解析，同时增加 ASCII 行兼容路径：USART3 ISR 仅缓存完整文本行，`SamplerComm_Poll()` 在主循环解析 `NTC:` 后 8 个十进制 raw 值，并转换成 16 字节小端 payload 调用 `SensorAcq_OnNtcFrame()`。
**原因**：当前硬件链路已经通，改主控解析能最快让现有采样板数据进入上层状态；解析放主循环避免在中断里做字符串扫描和整数转换。
**排除方案**：要求采样板立即改成 `AA 10 10 ... XOR` 二进制帧 — 协议更规整，但当前用户要求主控适配现有输出，先保留二进制兼容并新增 ASCII 兼容。

---

## [2026-06-17] BC260Y USART2 使用 9600 8N1 并保留 AT 探测缓存

**背景**：主控 USART2(PA2/PA3) 初始化为 115200 时，通过 J-Link 写 USART2 DR 发送 `AT\r\n` 后 `g_iot_rx_drops` 无增加；临时把 USART2 BRR 改为 9600 后发送 `AT\r\n`，`g_iot_rx_drops` 从 `0x0E` 增到 `0x12`，增加 4 字节，符合 `OK\r\n` 长度。
**决策**：USART2/BC260Y 固件配置改为 9600 8N1；`IotCtrl` 不再静默丢弃 RX 字节，改为保存最近 128 字节 raw ring，并提供 `g_iot_at_probe_req` 作为 SWD/J-Link 触发 AT 探测入口。
**原因**：BC260Y 当前实际波特率是 9600；保留 RX ring 能用 J-Link 运行态读回模块响应，避免外接 USB-UART 或暂停 CPU。
**排除方案**：继续用 115200 — 已实测无回包；直接用 J-Link 写 USART2 DR 抓 DR — 会与 ISR 抢字节且不便长期调试。

## [2026-06-17] BC260Y USART2 RX 启用内部上拉

**背景**：对照 `/Users/mac/Github/sleep` 的 BC260Y 工程发现其 PA3/USART2_RX 明确配置为内部上拉；主控板当前 PA3 为 `GPIO_NOPULL`。J-Link 运行态把 GPIOA PUPDR bit[7:6] 临时改为 pull-up 后，再触发 `AT\r\n`，此前出现的 `40/5C/FC/0D` 等乱码消失，RX 字节数变为 0。
**决策**：主控 USART2 初始化拆分 TX/RX：PA2 TX 保持 `GPIO_NOPULL`，PA3 RX 改为 `GPIO_PULLUP`。
**原因**：UART RX 空闲态应保持高电平；BC260Y 未响应或 TX 线未强驱动时，PA3 浮空会被误判为随机字节，污染 AT 探测结论。
**排除方案**：继续保留 `GPIO_NOPULL` 并在软件里过滤乱码 — 会掩盖物理链路问题，且无法区分真实 URC 与噪声。

## [2026-06-17] BC260Y 探针改为自动 AT 同步与行级诊断

**背景**：PA3 上拉后，连续 5 次 J-Link 触发 `AT\r\n` 均表现为 TX 增加、RX 为 0，说明当前没有真实 BC260Y 回包；但手动触发只能观察瞬时窗口，不利于模块稍后上电或从低功耗唤醒后的诊断。
**决策**：`IotCtrl` 保留 raw ring，同时新增可由 SWD 开启的 `AT\r\n` 同步：`g_iot_auto_probe=1` 后每 1s 发送一次直到收到 `OK`；ISR 记录最后一行、行计数、`OK`/`ERROR` 计数以及 ORE/NE/FE/PE 分类错误计数。
**原因**：对齐 sleep 项目的 BC260Y 诊断方式，给 J-Link 运行态读取提供稳定状态变量；默认关闭自动发送，避免 BC260Y POW/NET 未亮时通过 UART TX 反灌未上电模块。
**排除方案**：直接搬完整 MQTT 状态机 — 当前连基础 AT `OK` 都没有，完整 MQTT 会增加噪声和排查面。

## [2026-06-17] 物联网模块从 BC260Y 切换为 EC801E

**背景**：现场模块已从 BC260Y 换成 EC801E，且 EC801E 模块板 POW/NET 指示灯均未亮。原 BC260Y 9600 8N1 配置不再适合作为默认串口配置。
**决策**：USART2 保持 PA2/PA3、PA3 RX 内部上拉和 J-Link 可控 AT 探针，但默认波特率切换为 115200 8N1；自动 AT 探针默认关闭，等 EC801E POW 亮后再通过 SWD 写 `g_iot_auto_probe=1` 或 `g_iot_at_probe_req=1` 启动探测。
**原因**：EC801E 属于 Quectel EC/LTE Cat.1 系列，主 UART 常用 115200；POW/NET 均灭时先处理模块供电/启动，持续打 AT 可能通过 UART TX 反灌未上电模块。
**排除方案**：继续沿用 BC260Y 9600 配置 — 会把串口层排查建立在错误模块假设上；默认自动打 AT — 在模块未上电时没有诊断价值且可能增加反灌风险。

**验证补充**：烧录 App A 后，USART2 BRR=`0x016C`、PA3 pull-up 生效；未发送 AT 时收到 EC801E `^boot.rom...RDY\r\n` 启动输出，J-Link 触发 `AT\r\n` 后 raw ring 为 `AT\r\r\nOK\r\n`，`g_iot_at_ok=1`。

## [2026-06-17] EC801E 到 hk-newapi TCP 冒烟测试走通

**背景**：用户要求“打通 EC801E 到香港 newapi 名的服务器一个端口的通信”。本机 DNS 无法解析裸名 `newapi`，但 `~/.ssh/config` 中存在 `hk-newapi`，对应 `HostName 38.76.206.42`、`Port 22`。

**决策**：本轮先用 J-Link Commander 运行态写 USART2 DR 发送 AT 命令，不 halt、不 reset；目标端口选最确定的 `38.76.206.42:22` 做 TCP 可达性冒烟。EC801E 当前是中国电信网络，`AT+QICSGP=1` 显示 APN 空，因此将 PDP context 1 配为 `ctnet`：`AT+QICSGP=1,1,"ctnet","","",1`。

**验证证据**：
- 模块/链路：`ATI` 返回 `Quectel EC801E Revision: EC801ECNCGR07A03M02`；`AT` 返回 `OK`。
- 蜂窝注册：`AT+CPIN?` → `READY`；`AT+CSQ` → `22,99`；`AT+CEREG?`/`AT+CGREG?` → `0,1`；`AT+CGATT?` → `1`；`AT+COPS?` → `0,0,"CHN-CT",7`。
- PDP：`AT+QIACT=1` 返回 `OK`；`AT+QIACT?` → `+QIACT: 1,1,1,"10.56.122.255"`。
- TCP：`AT+QIOPEN=1,0,"TCP","38.76.206.42",22,0,0` 后返回 `+QIOPEN: 0,0`；`AT+QISTATE=0,1` 显示 socket 0 连接 `TCP` `38.76.206.42` remote port `22`。
- 下行数据：`AT+QIRD=0,128` 读到服务器 banner `SSH-2.0-OpenSSH_8.9p1 Ubuntu-3ubuntu0.15`；随后 `AT+QICLOSE=0` 返回 `OK`。

**结论**：EC801E 串口、SIM、蜂窝数据承载、到香港服务器 `38.76.206.42:22` 的 TCP 出口和下行接收均已验证。后续要做的是把这套 AT 序列固化成 `iot_ctrl` 初始化/连接状态机，并把目标从 SSH 冒烟端口换成业务实际端口和协议。

**排除方案**：
- 继续把 POW/NET 灯灭当作“模块未上电/未联网”判断 — 已被 `RDY`、`OK`、PDP IP、TCP open 和 SSH banner 证据排除。
- 继续寻找裸名 `newapi` 的本地 DNS 解析 — 当前 macOS DNS 无结果；本轮以 `hk-newapi` SSH 别名和 IP 为准。

---

## [2026-06-17] MQTT 管理后台 V1 采用 Mosquitto + PostgreSQL + FastAPI

**背景**：用户要求 EC801E 通过 MQTT 上报数据到香港 `newapi` 服务器，并且后端长期保存 GPS/采样/功率等数据，前端第一版先让管理员查看所有设备的实时数据和历史数据。

**决策**：在 `server/upboard_iot/` 下新增第一版后台：Mosquitto 监听 `1883`，FastAPI 同时承担 MQTT ingest、REST API、SSE 实时推送和静态前端，PostgreSQL 保存 `users/devices/user_devices/telemetry/device_events`。第一版只暴露管理员视图，`user_devices` 表先建好但不做普通用户权限 UI。

**原因**：MQTT 适合 EC801E 长连接小包上报；PostgreSQL 满足 GPS/历史数据长期查询；FastAPI + 静态前端部署简单，服务器只需 Docker Compose。

**验证证据**：
- `hk-newapi` / `38.76.206.42` 上 `upboard-iot-mqtt`、`upboard-iot-postgres`、`upboard-iot-web` 均运行。
- `curl http://127.0.0.1:18080/healthz` 返回 `{"ok":true,"service":"upboard-iot"}`。
- 服务器本地 MQTT publish 到 `upboard/UPB-DEMO-001/telemetry` 后，PostgreSQL `telemetry` 出现 `seq=1`，API `GET /api/devices` 能看到设备。
- 登录后订阅 `GET /api/stream`，再服务器本地 publish `seq=3`，SSE 返回 `event: telemetry` 且 PostgreSQL 出现同一条 `seq=3`。

**排除方案**：第一版直接做多租户用户设备绑定 UI — 会增加账号/权限面，延后到管理员全局视图验证稳定后再做。

## [2026-06-17] EC801E MQTT 发布采用 `QMTPUBEX` 长度模式

**背景**：EC801E 已经能 `QMTOPEN` 到 `38.76.206.42:1883` 并 `QMTCONN` 成功，但用 `QMTPUB` 进入 `>` 提示后通过 J-Link 写 USART2_DR 发送 JSON + Ctrl-Z，模块返回 `ERROR`，数据库无新增记录。

**决策**：EC801E 发布改用 `AT+QMTPUBEX=0,0,0,0,"upboard/<sn>/telemetry",<len>`，收到 `>` 后发送精确 `<len>` 字节 JSON，不再依赖 Ctrl-Z 结束符。调试时先 `ATE0` 关闭回显，避免长 JSON 回显挤爆 128 字节 RX ring。

**原因**：`QMTPUBEX=?` 已返回支持 `(0-5),<msgid>,(0-2),(0,1),"topic","length"`；长度模式把结束条件从交互控制字符变成确定字节数，更适合 STM32 状态机和 J-Link 运行态注入。

**验证证据**：
- J-Link Commander 运行态发送 `AT+QMTPUBEX=0,0,0,0,"upboard/UPB-DEMO-001/telemetry",203` 后，模块返回 `+QMTPUBEX: 0,0,0`。
- PostgreSQL `telemetry` 新增 `UPB-DEMO-001 seq=2`，字段包括 `rssi=22`、`latitude=22.300003`、`longitude=114.100004`、`ntc_raw={1891,10,9,10,2538,2540,2537,2537}`、`bat24_v=24.2`、`v12_v=12.5`。
- API `GET /api/devices/UPB-DEMO-001/latest` 返回同一条 `seq=2` 记录。

**排除方案**：继续调 `QMTPUB` + Ctrl-Z 时序 — 模块已明确返回 `ERROR`，且长度模式已经端到端通过。

---

## [2026-06-17] EC801E 自动 MQTT 状态机固化进 App

**背景**：EC801E、SIM、PDP、`QMTOPEN/QMTCONN`、`QMTPUBEX` 已通过 J-Link 手工 AT 序列和服务器入库验证。用户要求把自动初始化、重连、周期上报固化到 STM32。

**决策**：`IotCtrl` 从 AT 探针升级为运行态状态机：开机等待后执行 `ATE0/AT/CPIN/CSQ/CEREG/CGATT/QICSGP/QIACT/QMTCFG/QMTDISC/QMTCLOSE/QMTOPEN/QMTCONN`；连接成功后每 30 秒用 `QMTPUBEX` 发布 telemetry；遇到 `ERROR`、超时、`QMTSTAT` 或 publish 失败时进入 5s 起、最高 60s 的退避重连。设备 SN 由 STM32 UID 折叠生成，形如 `UPB-0B506761`。

**Telemetry 内容**：`sn/seq/uptime_ms/rssi/gps.fix=false/sampler.stale/sampler.ntc_raw[8]/power.bat24_v/power.bat24_i/power.v12_v/outputs.boost/load/fan/pump/compressor`。主循环每秒调用 `IotCtrl_SetTelemetrySnapshot()` 更新快照，状态机 publish 时使用最新快照。

**诊断变量**：保留原 `g_iot_rx_ring/g_iot_last_line/g_iot_at_ok/g_iot_at_error`，新增 `g_iot_state/g_iot_mqtt_connected/g_iot_pub_ok_count/g_iot_pub_fail_count/g_iot_seq/g_iot_last_payload_len/g_iot_last_csq/g_iot_device_sn/g_iot_last_cmd/g_iot_last_payload`，便于 J-Link 运行态直读。

**验证证据**：
- 主机侧 fake HAL 测试 `tests/iot_ctrl_mqtt_test.c` 通过，覆盖初始化命令序列、`QMTPUBEX` 长度发布、payload 不追加 Ctrl-Z、publish 成功计数。
- `cmake --build --preset app-a` 和 `cmake --build --preset app-b` 均退出码 0；当前 App A/B `text=31416, data=232, bss=5864`。
- J-Link Commander `loadfile build/app-a/app/upboard_A.hex` 显示 Program & Verify O.K.；额外 `verifybin` 因 `.isr_vector` 与 `.text` 之间未写入 gap 的 0x00/0xFF 差异失败，已用 HEX 段和 Flash 回读确认 0x08020188~0x0802018F 是未写 gap，不是烧录失败。
- App A 运行后 J-Link 读到：`g_iot_state=0x28`(CONNECTED)、`g_iot_mqtt_connected=1`、`g_iot_pub_ok_count=4`、`g_iot_pub_fail_count=0`、`g_iot_seq=4`、`g_iot_device_sn="UPB-0B506761"`。
- 服务器 PostgreSQL 入库 `UPB-0B506761 seq=1..5`，间隔约 30 秒；API `/api/devices/UPB-0B506761/latest` 返回 `seq=5`。
- 验证后已执行 J-Link `power off`，避免板子继续长时间通电。

**排除方案**：
- 继续依赖 J-Link 手工注入 AT 命令 — 无法长期运行，也无法验证掉线重连。
- 在主循环里阻塞等待每条 AT 响应 — 会影响 IWDG 和其他业务轮询；状态机只在发送 UART 字节时短暂阻塞，不等待网络响应。

---

## [2026-06-17] 采样板 GPS 沿用现有 MQTT `gps.lat/lon/fix` schema

**背景**：采样板当前 ASCII 行在有定位时输出 `GPS:<lat>,<lon>,<speed>kt`，无定位时输出 `GPS:no_fix`。服务器端 PostgreSQL / FastAPI 已支持从 MQTT JSON 的 `gps.lat`、`gps.lon`、`gps.fix` 入库和前端显示，但主控固件此前固定上报 `gps.fix=false`。

**决策**：不改采样板协议、不改服务器数据库；主控 `SamplerComm` 在解析 NTC 同一行时解析 GPS 字段，保存为定点快照（lat/lon ×1e6、speed knots ×1000），主循环灌入 `IotCtrl_TelemetrySnapshot`，`IotCtrl` 在采样板非 stale 且 fix=true 时上报 `{"gps":{"fix":true,"lat":...,"lon":...,"speed_kt":...}}`。

**原因**：采样板已有输出格式足够；服务器现有 schema 已能长期保存和前端显示经纬度；主控用定点数避免在解析路径引入浮点 `strtod/atof` 和格式不确定性。

**验证证据**：`tests/sampler_comm_ascii_test.c` 覆盖 `GPS:22.300001,114.100002,0.3kt` 与 `GPS:no_fix`；`tests/iot_ctrl_mqtt_test.c` 覆盖 MQTT payload 中 `gps.fix=true/lat/lon/speed_kt`；`cmake --build --preset app-a` 和 `cmake --build --preset app-b` 均退出码 0。

**排除方案**：服务器新增独立 GPS 接口或数据库字段重构 — 第一版不需要；采样板改二进制帧 — 会扩大联调面，现有 ASCII 已经被主控稳定接收。

---

## [2026-06-17] 管理后台 GPS 地图采用 Leaflet + OpenStreetMap

**背景**：用户要求网页端把设备 GPS 信息接入地图 API，并在后台显示位置。后端 PostgreSQL 已长期保存 `telemetry.latitude/longitude/gps_fix`，并已有 `/api/devices/{sn}/gps` 历史点接口。

**决策**：第一版前端在设备详情页接入 Leaflet，使用 OpenStreetMap tile API，无 API key；从 `/api/devices/{sn}/gps?limit=100` 读取历史点，地图显示最新 marker，多点时绘制轨迹 polyline，GPS 历史列表点击后定位到对应点。

**原因**：不新增后端 schema，不改 STM32/MQTT payload；Leaflet 集成轻量，OpenStreetMap 无需申请 key，适合当前管理员后台快速验证真实 GPS 上报链路。

**验证证据**：远端 `hk-newapi` 上 `upboard-iot-web` 健康检查返回 `{"ok":true,"service":"upboard-iot"}`；远端静态 HTML/JS 包含 Leaflet、`gpsMap`、`renderGpsMap()` 和 `tile.openstreetmap.org`；PostgreSQL 中真实设备 `UPB-0B506761` 已有 2 条 `gps_fix=true` 经纬度记录；登录 API 查询 `/api/devices/UPB-0B506761/gps?limit=10` 返回这 2 个点；Chrome/Playwright 打开后台后检测到 Leaflet 已加载、地图容器已初始化、瓦片 4 张、marker 1 个、轨迹 path 1 条、GPS 列表 2 条。

**排除方案**：第一版接高德/百度地图 — 需要申请 key，并且 GPS WGS84 坐标在国内地图上还要做 GCJ-02/BD-09 转换；当前先用无 key 的 OSM 跑通端到端显示。

---

## [2026-06-17] 管理后台 UI 重做为深色工业控制台

**背景**：第一版管理后台功能可用，但视觉过于默认，设备列表、指标、地图、曲线和原始 payload 缺少层级；用户要求用 `taste-skill` 等前端审美规则重做界面。

**决策**：保留现有静态 `index.html/app.js/styles.css` 架构，不引入 React 或构建链；前端视觉改为深色工业控制台：左侧设备 rail、顶部 telemetry 标题与连接状态、状态指标 panel、GPS 地图主模块、24V 曲线/输出/NTC 侧栏、事件和 raw payload 底部区域。地图瓦片改用 CARTO `dark_all`，其底层 attribution 仍包含 OpenStreetMap；GPS 指标改为展示“最近有效 GPS”，避免最新 telemetry 无定位时和历史地图点互相矛盾。

**原因**：静态架构部署风险最低，适合当前 FastAPI 静态文件服务；深色控制台比营销页风格更符合硬件/IoT 管理后台；地图和曲线是核心视觉资产，不需要额外装 Figma 或前端组件库。

**验证证据**：
- 本地 `node --check server/upboard_iot/backend/app/static/app.js` 退出码 0。
- taste-skill 关键禁区扫描通过：静态文件中未出现 em dash、Inter 默认、纯黑纯白、AI purple 等匹配项。
- 已 `rsync` 到 `hk-newapi:/opt/upboard-iot/backend/app/static/`，并只重建 `upboard-iot-web`。
- 远端 `curl http://127.0.0.1:18080/healthz` 返回 `{"ok":true,"service":"upboard-iot"}`。
- Chrome/Playwright 登录后台并选择 `UPB-0B506761` 后验证：页面标题为 `Upboard IoT Console`，深色背景生效，Leaflet 地图初始化，瓦片 12 张、GPS marker 1 个、轨迹 path 1 条、GPS 列表 2 条，顶部 GPS 显示 `29.717485, 106.830536`。
- 验证截图：`output/playwright/iot-console-redesign-map-wait.png`。

**排除方案**：引入 React/shadcn/Radix 重搭后台 — 当前需求只是单页管理台，新增构建链会增加部署面；Figma skill 先出设计稿 — 当前用户要求直接开始，且已有真实页面可浏览验证。

---

## [2026-07-11] 压缩机改为开机音结束后自动启动

**背景**：当前压缩机必须等待 C3 `SET_GEAR on` 才解除 DRVOFF 并发送速度指令。J-Link 已确认 gear 启动帧能够到达，但用户要求不依赖 C3，主控上电后自动发起制冷链路启动。现场同时读到 `GATE_DRIVER_FAULT_STATUS=0x82000000`，按 TI MCF8329A 数据手册解码为 `GVDD_UV_FLT`。

**决策**：保留原 gear on/off 控制；在阻塞式开机音结束后，若不处于 Motor Studio I2C 独占模式，先开启风扇和水泵，等待 500ms，再清锁存故障、tickle MCF watchdog、解除 DRVOFF 并发送 100% duty。周期故障恢复同时处理 controller fault 和排除 DRV_OFF 状态位后的 gate fault。

**原因**：把自动启动放在开机音之后，可避免压缩机已运行但 MCF watchdog 数秒无人维护；水泵/风扇先行提供确定的启动顺序；处理 gate fault 可避免一次瞬态 GVDD/BST/VDS 故障永久锁死。

**排除方案**：把 `g_mcf_spin_duty` 全局默认值直接改为 100% — 会在 MCF 初始化阶段过早起转，并跨越阻塞式开机音造成 watchdog 风险；完全删除 gear off 路径 — 会失去运行期正常停机能力。

## [2026-07-11] Gate fault 只在开机自动启动前清一次，不做无限重试

**背景**：首次自动启动实测已进入 `OPEN_LOOP(0x07)`，随后因现场板 `GVDD_UV_FLT` 回到 `FAULT(0x0E)`。若主循环对 gate fault 每 200ms 执行 `CLR_FLT`，状态会在启动与故障之间反复切换。

**决策**：保留开机自动启动前的一次 `ClearFault()`；主循环仍只对 controller fault 做周期重试，不自动清除 GVDD/BST/VDS 等 gate fault。

**原因**：gate fault 表示栅极电源、bootstrap、VDS过流等功率级条件异常；在物理条件未排除前反复清除会持续冲击驱动。硬件修复后通过重新上电即可重新执行自动启动流程。

**排除方案**：持续自动清 gate fault 直到起转 — 当前 GVDD欠压实测会反复复现，无法解决电源根因且降低安全性。

---

## [2026-07-11] MCF8329A 每次开机使用 TI Recommended Defaults，仅覆盖 shadow

**背景**：MCF8329A 上电会先把 Motor Studio 曾写入 EEPROM 的参数加载到 runtime shadow；因此仅仅不调用自定义参数表，仍然会使用用户烧录过的电机参数。用户要求本次运行完全改用官方默认参数。

**决策**：按 TI 数据手册 SLLSFQ7 Table 8-1 的 24 项 `Recommended Default Values`，在电机停止且 DRVOFF 有效时逐项写入 runtime shadow，并逐项读回校验；`DEVICE_CONFIG1` 因包含 I2C target address，放在最后写入并重新探测 target `0x01`。不发送 `EEPROM_WRT`，保留芯片内原有 EEPROM 内容。

**原因**：Table 8-1 是 TI 明确说明“chosen for reliable motor start-up and closed loop operation”的官方配置；寄存器章节中的 Reset 值大多为 0，不等同于推荐运行配置。仅写 shadow 可确保当前运行不使用旧 EEPROM 参数，同时保留随时回退能力。

**排除方案**：把全 0 Reset 值当成官方运行参数 — `INT_ALGO`、`GD_CONFIG` 等关键字段为 0，且与 TI Table 8-1 推荐配置不一致；直接把 Table 8-1 写回 EEPROM — 会不可逆覆盖用户现有 Motor Studio 参数，本次没有必要。

**验证补充**：App A/B 编译通过，`text=32900, data=236, bss=5924`；App A 经 OpenOCD/J-Link 烧录显示 `Verified OK`。按当次 ELF 重新解析符号后，运行态读到 `g_mcf_cfg_rc=0`、`g_mcf_cfg_mismatch=0`、`g_mcf_cfg_verified=24`，且 STARTUP1/STARTUP2/PIN_CONFIG/FAULT_CONFIG1/CL1/CL4/DEVICE_CONFIG2 均与 Table 8-1 一致。用户实测空载可勉强起转；采样时 `GATE_DRIVER_FAULT_STATUS=0x01000000` 仅为 `DRV_OFF` 状态，controller fault 为 0。

---

## [2026-07-11] MCF8329A 参数源增加可切换 profile，第一轮回到 ZW50 近似配置

**背景**：TI Table 8-1 官方通用参数已验证 24/24 写入成功，但用户实测空载仅勉强起转，说明通用配置对当前压缩机的电机模型和启动过程裕量不足。仓库已有 `BLDC_Pump_3A_200Hz` 派生的 ZW50 近似参数，历史上曾让同系列压缩机物理起转。

**决策**：新增 `g_mcf_config_profile`：`1=TI Table 8-1`、`2=ZW50/3A/200Hz compiled profile`，默认先试 profile 2。两套配置均只覆盖 runtime shadow、逐项读回 24 项，不执行 EEPROM commit；`DEVICE_CONFIG1` 最后写并处理 I2C 地址重绑定。

**原因**：在不破坏原 EEPROM 的前提下保留可重复 A/B 对照；第一轮只改变参数 profile，保持速度指令、GPIO 时序和故障策略不变，结果可明确归因。

**排除方案**：承诺一组通用参数必然带载启动 — sensorless FOC 还依赖当前电机 Rs/L/Ke、压缩机压差以及母线/GVDD；跳过运行态验证直接写 EEPROM — 无法安全回退。

---

## [2026-07-13] 水泵采用 PC11 供电软 PWM，默认 30% 占空比

**背景**：V6 水泵只有 PC11 `PUMP_CTRL` 两线供电开关，没有独立调速输入，也没有可直接映射到 PC11 的定时器 PWM 通道。用户要求把水泵降到 30%。

**决策**：复用现有 TIM7 2kHz 更新中断驱动 PC11，生成 100Hz 供电 PWM；默认运行占空比 30%，每次开启先保持 1 秒全功率再进入 PWM。保留 `PowerCtrl_EnablePump()` 开关语义，并新增占空比设置接口和 VSCode Watch 可见诊断变量。

**原因**：固定定时器时基不受主循环 I2C 扫描和阻塞任务影响；复用 TIM7 不增加定时器资源占用；全功率启动可降低低占空比直接启动时的堵转风险。

**排除方案**：在主循环按 `HAL_GetTick()` 通断 PC11 — 主循环存在最长数百毫秒的阻塞操作，PWM 会严重抖动；把 PC11 配成硬件 PWM — STM32F407 的 PC11 与当前板级连线没有对应定时器输出；数秒级启停做平均流量 — 这不是稳定调速，并会让水泵反复启动。

**验证要求**：编译通过只验证固件实现；上板后必须在 PC11 测到启动约 1 秒持续 HIGH，随后 100Hz、30% 占空比（高约 3ms、低约 7ms），并用转速计确认实际转速。若水泵规格禁止供电 PWM，不得启用该方案。

---

## [2026-07-13] J-Link 烧录脚本遇 OpenOCD 错误必须立即失败

**背景**：`flash_app_a_jlink_once.sh` 原先只启用 `set -uo pipefail`。本次 OpenOCD 报 `Error connecting DP: cannot read IDR` 后，脚本仍继续打印 `[4/4] Verified OK` 并以 0 退出，违反“未经验证不得报告成功”的项目硬规则。

**决策**：脚本改为 `set -euo pipefail`，任何构建、供电或 OpenOCD program/verify 命令返回非零时立即退出，不再执行成功提示。

**原因**：OpenOCD 的退出码才是 program/verify 是否完成的可信判据；成功文案不能脱离退出码单独执行。

**排除方案**：仅依赖日志字符串人工判断 — 自动化调用仍会拿到错误的 0 退出码，后续流程会继续传播假成功。

---

## [2026-07-23] 水泵 30% 软 PWM 完成 App A 烧录与运行态验证

**背景**：2026-07-13 首次烧录因 SWD 无法读取 DAP 而中止；用户修复连接后要求重新烧录。

**验证证据**：J-Link 读到 DPIDR `0x2BA01477`、DBGMCU ID `0x10076413`、BKP0R `0xA0A0A0A0`，确认当前为 Slot A。OpenOCD 对 `upboard_A.hex` 返回 `Programming Finished`、`Verified OK` 并复位。按本次 ELF 重新解析符号后，运行态读到 `g_pump_duty_pct=0x1E`、`g_pump_startup_ticks=0`、TIM7 CR1=`0x81`、DIER=`0x01`，连续采样 `g_pump_pwm_phase` 递增且 `g_pump_output_on` 从 1 切换到 0。

**剩余验证**：SWD 证据确认固件和软件 PWM 状态机正在运行；仍需示波器测 PC11 为 100Hz、30%（约 3ms HIGH / 7ms LOW），并用转速计确认水泵实际转速且无堵转或反复重启。

---

## [2026-07-23] V7 压缩机从 MCF8329A/I2C3 切换为 PWM + DIR + STOP

**背景**：用户确认硬件已改版：原风扇 PA15/TIM2_CH1 改接压缩机 PWM，NE14 接 PC9 控制正反转，NE15 接 PA8 控制停转。

**决策**：V7 压缩机控制接口以 PA15/TIM2_CH1(AF1) PWM、PC9 GPIO DIR、PA8 GPIO STOP 为目标架构；原 MCF8329A I2C3 控制路径判定为废弃，待新版原理图和驱动器规格确认后从固件初始化及周期任务中完整移除。

**原因**：PA15 的 TIM2_CH1 复用在 STM32F407 上成立；PC9/PA8 可作 GPIO。但当前 PA15 属于风扇控制，PA8/PC9 属于 I2C3，若只局部改引脚而不删除旧路径，会出现压缩机误全速和 DIR/STOP 被 AF4 覆盖的确定性冲突。

**排除方案**：保留 I2C3 同时把 PA8/PC9 当 GPIO — 同一引脚无法同时承担 AF4 和输出 GPIO；继续复用 `FanCtrl_SetDuty()` 驱动压缩机 — 命名、启动时序和 gear 逻辑都会把风扇行为错误映射为压缩机命令，风险不可接受。

**主控连线证据**：V6 主控网表中 PC9(U9.66) 经 R10 0Ω 接 FPC2.14/NET14，PA8(U9.67) 经 R11 0Ω 接 FPC2.15/NET15；主控到排线的映射成立。

**待确认**：新版驱动板修订原理图、驱动器输入电平、PWM 频率/极性/占空比范围、PC9 方向电平、PA8 STOP 有效电平与安全默认态、风扇迁移后的控制接口。

---

## [2026-07-22] 新版遥测前端采用独立 Vite 应用和可替换数据适配层

**背景**：现有 MQTT 管理后台由 FastAPI 直接托管原生 HTML/CSS/JavaScript，已经部署并可用；本次需要按 Premium Telemetry Dashboard 规格实现 React、TypeScript、Vite、Tailwind CSS、shadcn/ui、ECharts、Leaflet、Zustand 和 React Query，同时先使用 Mock 数据。

**决策**：在 `server/upboard_iot/frontend/` 新建独立 Vite 应用，暂不覆盖 `backend/app/static/`。领域类型集中在 `src/types`，Mock 数据集中在 `src/mocks`，组件仅通过 `src/services/telemetry` 数据服务取数；React Query 管理异步缓存和刷新，Zustand 管理设备选择、筛选、时间范围及连接演示状态。地图和 ECharts 作为懒加载组件。

**原因**：独立构建可避免未完成版本影响当前线上后台；数据适配层使 Mock、REST、SSE/WebSocket 和 MQTT over WebSocket 可在不改 UI 组件的情况下替换；按设备 SN 组织 query key 可保证切换设备时所有遥测模块同步更新。

**排除方案**：直接重写 `backend/app/static/` — 会把视觉开发风险带到当前已部署版本；在组件内部直接声明 Mock 数据 — 后续接真实接口需要逐组件重构；由 Zustand 保存全部服务器数据 — 会复制 React Query 的缓存职责并增加一致性问题。

---

## [2026-07-22] THERMORIDE 使用 REST 初始快照、SSE 增量刷新和 FastAPI 同源托管

**背景**：React 前端已完成 Mock 验收，现有 FastAPI 已提供 Cookie 会话、设备/遥测/GPS/事件 REST 接口与 `/api/stream` SSE，但生产镜像仍只包含旧静态页，Vite 开发服务器也尚未代理 API。

**决策**：前端默认使用真实 API，保留 `VITE_DATA_SOURCE=mock` 作为显式演示开关；React Query 持有服务端数据，Zustand 只持有设备选择、筛选和连接状态；页面先通过 `/api/me` 鉴权，登录成功后加载 REST 快照，再用同源 SSE 按设备 SN 失效并刷新相关查询。Vite 开发环境代理 `/api` 与 `/healthz` 到 FastAPI。生产部署改为 Node + Python 多阶段镜像，将 `frontend/dist` 复制到 FastAPI 的独立 SPA 目录并提供 history fallback，同时保留旧 `backend/app/static` 文件作为源码回退资产，不在构建中删除。

**原因**：同源 Cookie 不需要把 token 暴露给 JavaScript；REST 适合首次加载和断线恢复，SSE 足以承载服务器到浏览器的单向遥测通知；查询失效避免维护第二份实时数据缓存；多阶段构建让当前 React 产物与后端版本一起发布且可重复构建。

**排除方案**：浏览器直接订阅 MQTT over WebSocket - 需要暴露 broker 凭据并新增 ACL/TLS 面；把 SSE payload 直接合并进 Zustand - 会与 React Query 形成双缓存；生产继续分别运行 Vite 与 FastAPI - 增加跨域、Cookie 和部署运维复杂度；覆盖或删除旧静态源码 - 回退路径不清晰且不利于审计。

---

## [2026-07-22] Docker Hub 不可达时使用已验证运行时镜像叠加发布

**背景**：`hk-newapi` 首次执行新版多阶段构建时，Docker Hub 的 `auth.docker.io:443` 匿名 token 请求持续超时；旧 `upboard-iot-web` 容器和本地构建均正常，PostgreSQL 与 MQTT 不能因镜像源故障中断。

**决策**：保留 `backend/Dockerfile` 作为标准、可重复的 Node + Python 多阶段构建；本次先把旧运行镜像按内容 ID 标记为 `upboard-iot-web:legacy`，上传已通过本地 `npm run build` 的 `frontend/dist`，使用 `backend/Dockerfile.offline` 仅叠加新版 `backend/app` 与前端产物，再以 `docker compose up -d --no-deps --no-build web` 替换 Web 容器。部署前备份原 Compose、Dockerfile 和 `main.py` 到 `/opt/upboard-iot/backups/thermoride-20260722-174952`。

**原因**：离线叠加复用服务器已运行 5 周的 Python 依赖层，不访问外部镜像仓库；替换范围只包含 Web 容器，数据库、MQTT 和数据卷不重建；标准 Dockerfile 仍是网络恢复后的正式构建路径。

**排除方案**：等待 Docker Hub 网络恢复后再部署 - 用户已明确要求当前部署；重建 PostgreSQL/MQTT 或执行整套 `docker compose up --build` - 会扩大停机与数据风险；直接 `docker cp` 修改运行容器 - 不可复现且难以回滚。

---

## [2026-07-22] 系统健康指标点必须反映连接和指标状态

**背景**：系统健康卡片的四个指标点原本固定使用绿色，即使设备显示 Offline 仍像是在表达实时正常，和“最后已知数据”的语义冲突。

**决策**：指标点先受设备连接状态约束：Offline 统一灰色、Delayed 统一橙色、Fault 统一红色；Online 时再按单项状态显示绿色、橙色或灰色。指标数值仍保留为最后已知数据，不因离线而清零。

**原因**：颜色必须表达数据新鲜度与告警语义，不能只作为装饰；保留最后已知数值可用于诊断，但灰点明确提示这些值不是实时状态。

**排除方案**：离线时继续显示绿色并只依赖顶部 Offline 标签 - 用户容易把局部绿点解读为四项实时正常；离线时隐藏整个指标区 - 会丢失有价值的最后已知数据。

---

## [2026-07-22] 离线设备的执行器布尔值只作为最后已知状态

**背景**：设备离线后，执行器面板仍把数据库最后一帧的 `true` 显示为绿色 `Active`，容易被理解为当前仍在运行。

**决策**：当设备为 Offline 时，所有执行器状态统一映射为灰色 `Unknown`；原始布尔值不丢弃，描述显示 `Last known: active/inactive before disconnect`。设备在线时仍按最新布尔值显示 Active 或 Off。

**原因**：离线期间无法证明物理执行器的当前状态，最后一帧只能作为历史信息；状态标签和描述分开表达可同时避免误导并保留诊断价值。

**排除方案**：离线时继续显示 Active/Off - 会把历史值冒充实时值；离线时全部显示 Off - 同样是未经证实的当前状态，并可能掩盖断线前仍处于开启状态的信息。

---

## [2026-07-22] 管理员凭据变更同时旋转会话密钥

**背景**：管理员用户名和密码需要变更。后端启动时的 `ensure_admin()` 只在用户不存在时插入记录，不会更新已有密码；仅修改 `.env` 会留下旧数据库账号和已签名 Cookie。

**决策**：原位更新 PostgreSQL 中现有管理员记录以保持用户 ID 不变，同步修改服务器 `.env` 的管理员配置，并旋转 `SESSION_SECRET` 使所有旧 Cookie 立即失效。删除重启过程中因旧 shell 环境变量优先级而短暂重建的旧管理员，并从 `USAGE.md` 移除硬编码生产凭据。

**原因**：数据库记录、运行容器配置和会话签名必须一致；旋转密钥可防止旧登录会话在密码变更后继续使用；文档只引用 `.env` 可避免凭据进入版本库。

**排除方案**：只改 `.env` - 已有数据库密码不会更新；只改数据库 - 后续容器启动可能按旧环境重新创建管理员；保留旧会话直到自然过期 - 不符合凭据变更后的立即失效预期。
