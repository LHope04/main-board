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
