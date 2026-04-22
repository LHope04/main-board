---
name: stm32-auto-iterative-dev
description: "Cortex-M 项目通用迭代开发主循环：探测 preset → 知识库检索 → GPIO 确认 → 实验设计 → 改代码 → cmake --build → ST-Link 烧录 → 内存直读验证（首选）/ 串口（备用）→ 失败时按软硬分离决策下一步。任何 CMake + arm-none-eabi-gcc + ST-Link 工程通用。用户说继续调试 / keep iterating until it works / 自己搞定 时进入。"
---

# Skill-5：迭代开发主循环（CMake + arm-none-eabi-gcc + ST-Link，通用）

## 触发时机

- 模块已拆解（skill-4 完成），进入具体开发
- 用户说：继续调试 / keep going / 自己搞定 / auto iterative development / write code and debug it yourself / keep iterating until it works / continue debugging until success

---

## 主循环

```
探测 preset → 知识库检索 → GPIO 确认 → 实验设计 → 修改代码
  → cmake --build → 烧录 → 内存直读验证 → 失败分析（软硬分离）
                                              ↑
                                              └── 未通过，下一轮 ──┐
                                                                  ↓
                                                          下一轮假设
```

---

## Step 0 — 知识库检索（每次进入 skill-5 时执行）

根据本次任务涉及的外设和工具，检索 knowledge-base 对应文件的匹配条目，作为本轮已知约束主动规避：

| 涉及内容 | 检索文件 | 关键词 |
|----------|----------|--------|
| TIM / PWM / 输入捕获 | `known-bugs.md` + `hal-patterns.md` | TIM · PWM · IC · 中断 · 双向量 |
| I2C / SPI | `known-bugs.md` + `hal-patterns.md` | I2C · SPI · TIMEOUT · ACK · NACK · BUSY |
| UART / 串口输出 | `hal-patterns.md` | printf · newlib · _write · snprintf · uart |
| 中断 / 共享变量 | `hal-patterns.md` | volatile · 快照 · 竞态 · 原子 |
| ST-LINK 内存读取 | `known-bugs.md` | r32 · HotPlug · count |
| OTA / Bootloader | `known-bugs.md` | VECT_TAB_OFFSET · MSP · IWDG · 跳转 |
| 调试 / 验证阶段 | `debug-recipes.md` | 串口 · 日志 · 过期 · 内存读取 |

---

## Step 1 — 路径与 preset 列表（每次新会话探测一次）

不假设 preset 名，先发现：

```bash
# 1. 找 CMakePresets.json
test -f CMakePresets.json && echo found

# 2. 列出非 hidden 的 configurePreset
python -c "
import json
p = json.load(open('CMakePresets.json'))
for x in p.get('configurePresets', []):
    if not x.get('hidden'):
        print(x['name'], '->', x.get('binaryDir', '(default)'))
"

# 3. STLINK 路径从 CLAUDE.md 取（已固化），或本机搜：
ls "/c/Program Files (x86)/STMicroelectronics/STM32 ST-LINK Utility/ST-LINK Utility/ST-LINK_CLI.exe" 2>/dev/null

# 4. 如有串口监听脚本：
SERIAL_MON=$(find tools -name "serial_monitor.py" 2>/dev/null | head -1)
[ -n "$SERIAL_MON" ] && PYBIN=$(python -c "import serial,sys; print(sys.executable)")
```

把发现到的 preset 列表与 STLINK 路径写入本会话上下文。

> 工程的 **调试串口规则** 见 `CLAUDE.md`（哪些 UART 不能用作调试输出）。Bootloader 阶段尤其要注意。

---

## Step 2 — 选择受影响的 preset

通用规则（按改动范围决定要重编哪些 preset）：

| 改动位置 | 重编 |
|---|---|
| 仅某个 image 私有 src/inc | 该 image 对应的 preset |
| `Common/`、共享 `Core/Src/system_*.c`、HAL 驱动 | 全部 preset |
| 某个 image 的链接脚本 `*.ld` | 该 preset |
| 工具链文件 `cmake/*-gcc.cmake`、顶层 `CMakeLists.txt`、`CMakePresets.json` | 全部 preset，并先 reconfigure |

实在拿不准时直接全编：

```bash
PRESETS="<列出 Step 1 探测到的全部>"
for p in $PRESETS; do cmake --build --preset $p || break; done
```

ninja 增量会跳过未变 `.o`，多编几个 preset 的代价很低。

---

## Step 3 — GPIO / 接口映射确认（写外设代码前必须执行）

**开始写任何外设驱动代码前，先与用户确认完整的引脚和接口映射。**
部分引脚信息会导致开发到一半需要重构，代价极高。

对照 `.context/hardware.context.md`，确认：

```
□ 所有用到的引脚编号和复用功能（AF）与原理图一致
□ 电平标准匹配（3.3V / 5V 容忍）
□ 外部上下拉电阻是否存在
□ 调试串口规则未被违反（见 CLAUDE.md）
□ 任何不确定的引脚，必须在这里追问用户，不得假设
```

如果 `hardware.context.md` 中信息不完整，执行 skill-2 补全后再继续。

---

## Step 4 — 量化验收约定（每个新功能开始前）

**在开始开发一个功能前，先与用户约定量化的通过条件，而不是开发完再讨论"对不对"。**

按功能类型约定验收范围：

| 功能类型 | 量化验收示例 |
|----------|-------------|
| 输入捕获 / 频率测量 | "30~200 Hz 范围内，误差 < 2%" |
| ADC 读取 | "室温下 ADC 原始值在 1800~2200 之间" |
| GPIO 输出 | "ODR 寄存器值为 0x0020（bit5=1）" |
| PWM 占空比 | "CCR = 500，ARR = 1000，示波器量占空比 50% ± 1%" |
| I2C 传感器 | "温度读数在 15~40℃，湿度在 20~80%RH" |
| 串口通信 | "每秒收到一帧，CRC 校验通过率 100%" |
| OTA 流程 | "新固件 CRC 校验通过 + 槽位标记切到目标槽 + 复位后 main 入口落在目标 sector" |

未约定量化条件时，不得以"看起来合理"作为通过依据。

---

## Step 5 — 实验设计（每轮迭代开始前必须声明）

修改任何代码前，输出：

```
【本轮假设】：[例：I2C 读取失败的原因是设备地址错误，应为 0x76 而非 0x77]
【排除目标】：[例：本步可排除"SCL 时钟频率问题"和"数据手册寄存器读取逻辑错误"]
【判断条件】：
  通过 → [例：内存变量 g_temp_raw != 0，且在 1800~2200 范围内]
  失败 → [例：仍然超时，下步检查引脚上拉电阻]
```

原则：每轮只改一个主要变量，必须能产生明确的是/否结论，不允许"试试看"。

---

## Step 6 — 添加测试点

根据需要添加最小化测试点：

- 计数器变量（证明循环和周期任务在运行，通过内存直读确认）
- `snprintf` + `uart_send_string`（**禁止用 `printf`**，见 `hal-patterns.md` 的 `printf 在 newlib-nano 下无输出` 条目）
- 可被 ST-Link HotPlug 直读的全局变量
- GPIO 翻转（证明中断触发时序，配合示波器使用）

---

## Step 7 — 修改代码

修改前检查清单（适用于任何 Cortex-M）：

- 引脚和 AF 已在 Step 3 确认
- 外设时钟已使能（`__HAL_RCC_xxx_CLK_ENABLE`）
- 中断向量号、优先级与 NVIC 分组匹配
- 共享变量加 `volatile`，复合操作做临界区保护
- bootloader / 长初始化期间 IWDG 主动喂狗
- 共享 UART 上的自发帧（STATUS / 心跳 / 日志）必须被 OTA / 协议 BUSY 标志门控（避免污染 ACK 流）
- 知识库列出的 HAL 已知坑（见 Step 0 检索结果）已规避
- **所有 shell 命令用 bash 语法，禁止 PowerShell 的 `& "..."` 写法**

---

## Step 8 — 编译 + 资源预警

```bash
cmake --build --preset <name>
```

**通过标准**：
- ninja 退出码 0
- 链接阶段输出 `Memory region Used Size` 表
- 没有 `error:` / `undefined reference:` / `region overflow`

失败时直接看 ninja 报错那一行（路径就在错误里），打开文件定位、修、重跑当前 preset。

**资源预警**（与 `.context/project.context.md` 基线对比；阈值按本工程 image 容量自行换算）：

```bash
arm-none-eabi-size build/<preset>/<sub>/<image>.elf
```

| 条件 | 动作 |
|------|------|
| App image text+data > 槽位容量 80% | ⚠️ 询问是否需要优化 |
| RAM data+bss > 总 RAM 70% | ⚠️ 检查堆栈和全局变量 |
| 单轮 text 增量 > 5 KB | ⚠️ 询问原因，是否引入了不必要的库 |
| 单轮 bss 增量 > 1 KB | ⚠️ 询问原因，检查大数组或缓冲区 |
| `region 'FLASH' overflowed` | ⛔ 立即停，不能烧录 |

---

## Step 9 — 烧录

按修改的 image 烧对应文件。多 image 烧录时**只在最后一个 image 加 `-Rst`**。

```bash
STLINK="<STLINK_CLI_PATH>"   # 见 CLAUDE.md

# 单 image / 改了某 App
"$STLINK" -c SWD UR -P build/<preset>/<sub>/<image>.hex -V after_programming -Rst

# OTA 工程切槽位（写 params 区）
"$STLINK" -c SWD UR -P build/<preset>/<sub>/<params>.bin <params-addr> -V after_programming -Rst
```

**通过标准**：每条命令都必须打印 `Programming Complete` 和 `Verification...OK`。

| 失败现象 | 处理方式 |
|----------|----------|
| `No ST-LINK detected` | 检查 USB / 供电，停止并上报，不继续 |
| `Can't reset the core` | 改用 HotPlug 模式，或对板子重新上电 |
| 未见 `Verification...OK` | 不得声称烧录成功；先查是否触动了写保护，再重试 |
| App 烧完不跑 | 读槽位标记 + reset handler 入口，确认（如有）bootloader 跳到对的 sector |

⛔ 禁止 `-ME`（mass erase）作为"清理"动作 —— 会同时清掉 bootloader、params、其他 App 槽。

---

## Step 10 — 验证行为

### 主通道：内存直读（默认，优先使用）

```bash
# 1. 从对应 image 的 MAP 文件查找变量地址
grep -E "\b<symbol>\b" build/<preset>/<sub>/<image>.map

# 2. HotPlug 读取（不打断 CPU）—— 一次只读 1 个 word
"$STLINK" -c SWD HotPlug -r32 0x2000xxxx 1

# 多变量逐个读
"$STLINK" -c SWD HotPlug -r32 <addr1> 1
"$STLINK" -c SWD HotPlug -r32 <addr2> 1

# OTA 工程的槽位标记（地址见 CLAUDE.md <SLOT_MARK>）
"$STLINK" -c SWD HotPlug -r32 <slot_mark_addr> 1
```

内存直读优先原因：不需串口连线 · 不打断 CPU · COM 口被占用时仍然有效 · 任何阶段可用。

### 备用通道：逻辑分析仪 CSV（如有）

```bash
cat "$(find tools -name 'logic_analyzer.csv' 2>/dev/null | head -1)"
```

读取 Saleae 等工具导出的解码帧，分析 I2C/SPI/UART 的 ACK/NACK、帧序列、时序关系。

### 后备通道：串口日志（仅当内存读取无法回答问题时）

> ⚠️ 工程可能有 UART 被协议占用（见 `CLAUDE.md` 的"调试串口规则"段）。
> Bootloader / 工具调试在受占用 UART 上必须保持静默，只能用 LED 或内存读。
> App 调试串口请使用本工程约定的端口（参考 `hardware.context.md`）。

**第一步：skill 主动发现 COM 口**

```bash
python -c "
import serial.tools.list_ports
[print(p.device, p.description) for p in serial.tools.list_ports.comports()]
"
```

**第二步：skill 主动启动 serial_monitor.py**

```bash
"$PYBIN" "$SERIAL_MON" <COM_PORT> 115200 &
sleep 8
```

**第三步：skill 验证日志时效性后读取**

```bash
SERIAL_LOG=$(dirname "$SERIAL_MON")/serial_log.txt
stat "$SERIAL_LOG" | grep Modify   # 必须晚于最近一次烧录
cat "$SERIAL_LOG"
```

**异常处理（skill 自主决策，不等待用户）**：

| 异常 | skill 的处理动作 |
|------|-----------------|
| COM 口报 `PermissionError` | 立即切换内存直读，不等待不重试 |
| `$SERIAL_MON` 在 Step 1 未找到 | 声明串口不可用，继续使用内存直读 |
| 日志修改时间早于烧录时间 | 重启 serial_monitor.py，重新等待 8 秒后再读 |
| 日志为空 | 检查波特率是否与固件一致，或切换内存直读 |

### 进阶：OpenOCD + GDB（需要源码级单步时）

VSCode 直接 F5（建议在 `.vscode/launch.json` 配 Cortex-Debug，每个 image 一套 + 一套 Attach）。
命令行 OpenOCD 模板见 `CLAUDE.md` 的"调试 / 内存读取"段。

---

## Step 11 — 常见问题检查（Common Checks）

### 编译失败

```bash
cmake --build --preset <name> 2>&1 | tail -40   # 看 ninja 报错的实际行
```

定位实际报错原因后再改代码，禁止盲目修改。常见：
- `undefined reference to 'xxx'` → 缺源文件或 CMakeLists 漏 `target_sources`
- `region overflowed` → 链接脚本 LENGTH 不够，或 `--gc-sections` 没生效
- `multiple definition` → CMakeLists 里同一个 `.c` 被双重加入

### 烧录失败

- `No ST-LINK detected` → 硬件未连接，停止
- `Can't reset the core` → 改用 HotPlug 或重新上电
- `Verification failed` → 多半触动了写保护或 SWD 干扰；不要 mass-erase 重试

### 串口无输出 / printf 无效

- 见 `hal-patterns.md` 的 `printf 在 newlib-nano 下无输出` 条目
- 改用 `snprintf + uart_send_string`，完全绕开 newlib retarget

### 传感器 / 外设数值始终为 0 或异常

```bash
# 第一步：先读原始寄存器或变量，不要先看处理后的值
"$STLINK" -c SWD HotPlug -r32 <raw_register_addr> 1
```

- 原始值在变化但处理后结果异常 → 检查符号约定、比例因子、偏移量
- 原始值始终为 0 → 检查以下项：

```
□ GPIO 时钟是否使能（__HAL_RCC_GPIOx_CLK_ENABLE）
□ 引脚模式是否正确（输入/输出/复用/模拟）
□ 复用功能（AF）编号是否正确
□ 外设时钟是否使能（__HAL_RCC_TIMx_CLK_ENABLE 等）
□ SCL/SDA 或 CLK/DATA 的物理连线假设是否与代码一致
□ I2C 运行期间没有切换 SCL/SDA 的 GPIO 模式（会永久 BUSY）
```

### App 烧完不跑（OTA 跳转链路问题，仅 OTA 工程）

```bash
# 1. 槽位 mark（地址 + 期望值见 CLAUDE.md <SLOT_MARK>）
"$STLINK" -c SWD HotPlug -r32 <slot_mark_addr> 1

# 2. Bootloader 实际跳的入口（vector 表 reset handler）
#    应落在目标 image 的 Flash 区间内
"$STLINK" -c SWD HotPlug -r32 <image_base+4> 1

# 3. 初始 MSP 是否在 SRAM 范围内
"$STLINK" -c SWD HotPlug -r32 <image_base> 1
```

如果槽位标记对、reset handler 对，但应用还是不跑 → 多半是 `VECT_TAB_OFFSET` 没匹配链接脚本里的 FLASH origin。

---

## Step 12 — 失败分析：软硬分离

验证未通过时，必须将嫌疑分两列列出，再决定从哪侧切入：

```
【软件侧嫌疑】                    【硬件侧嫌疑】
- 设备地址配置错误                - SCL/SDA 引脚接错或未焊好
- 初始化顺序错误                  - 外部上拉电阻缺失或阻值过大
- 寄存器写入顺序错误              - 传感器供电电压不足
- 超时时间设置过短                - 总线被外部器件拖死
- 符号/比例因子错误               - 电源纹波影响时序
- VECT_TAB_OFFSET 与链接脚本不匹配 - SWD 接触不良
```

切入原则：优先排查软件侧（可通过读寄存器直接验证），软件侧完全排除后再进入硬件侧。

---

## Step 13 — 假设排除日志

每轮结束时记录（写入 `.context/decisions.log.md`，防止下一轮兜圈子）：

```
轮次 N：假设[xxx]，结果[成立/不成立]，排除了[yyy]，下轮方向[zzz]
```

---

## Step 14 — 硬件移交协议

当以下条件**同时满足**时，停止软件迭代，输出精确移交报告：

- 所有相关寄存器已通过内存直读确认配置正确
- 代码逻辑经过 mock 测试或静态分析确认无误
- 3 轮以上迭代后现象仍未改变

**移交报告格式**（禁止泛化表述"可能是硬件问题"）：

```
【已排除（软件侧，附证据）】
- I2C 地址：0x76（内存直读 CR1=0x0001 PE=1 已确认）
- SCL 频率：100kHz 标准模式（时钟树计算正确）
- 代码逻辑：已通过 mock 验证 ACK 正常时读取路径正确

【当前嫌疑（硬件侧）】
- SCL/SDA 实际波形未确认
- 外部上拉电阻实际阻值未测量

【建议人工操作】
1. 示波器探 SCL 引脚，确认有 100kHz 波形
2. 示波器探 SDA 引脚，确认地址帧第 9 个时钟 SDA 被拉低（ACK）
3. 万用表量传感器 VCC 引脚，确认 3.3V ± 5%
```

---

## 执行规则

- 每次新会话执行 Step 1（探测 preset 列表 + STLINK 路径）。
- **所有 shell 命令使用 bash 语法**，禁止 PowerShell 的 `& "..."` 写法。
- 读取运行中目标内存时必须使用 `HotPlug`，省略会导致 `Can't reset the core`。
- 验证默认使用内存直读，只在内存读取无法回答问题时才使用串口。
- 工程指定的协议 UART 永远不能作为调试串口（具体见 `CLAUDE.md`）。
- 串口日志使用前必须确认修改时间晚于最近一次烧录。
- 不得在单次失败后停止，除非遇到具体的外部阻塞。
- 不得在没有量化验证结果的情况下报告成功。
- **开始写任何外设代码前，必须先与用户确认完整的 GPIO / 接口映射（Step 3），部分引脚信息会导致开发中途返工。**
- 多 image 烧录时只在最后一个加 `-Rst`；禁止 `-ME` mass erase 作为清理动作。
- 遇到硬件阻塞或工具不可用时，精确描述阻塞原因后停止，不进行推测性迭代。

---

## 命令速查

| 操作 | 命令 |
|------|------|
| 列 preset | `python -c "import json; [print(x['name']) for x in json.load(open('CMakePresets.json'))['configurePresets'] if not x.get('hidden')]"` |
| 配置某个 preset | `cmake --preset <name>` |
| 编译 | `cmake --build --preset <name>` |
| 全部 preset 编译 | `for p in $PRESETS; do cmake --build --preset $p \|\| break; done` |
| 看 size | `arm-none-eabi-size build/<preset>/<sub>/<image>.elf` |
| 烧 image | `"$STLINK" -c SWD UR -P build/<preset>/<sub>/<image>.hex -V after_programming -Rst` |
| 写 OTA params | `"$STLINK" -c SWD UR -P build/<preset>/<sub>/<params>.bin <params-addr> -V after_programming -Rst` |
| 内存直读（主） | `"$STLINK" -c SWD HotPlug -r32 <addr> 1` |
| 槽位 mark（如有 OTA） | `"$STLINK" -c SWD HotPlug -r32 <slot_mark_addr> 1` |
| 查变量地址 | `grep -E "\b<symbol>\b" build/<preset>/<sub>/<image>.map` |
| 列出 COM 口 | `python -c "import serial.tools.list_ports; [print(p.device, p.description) for p in serial.tools.list_ports.comports()]"` |
| 启动串口监听 | `"$PYBIN" "$SERIAL_MON" <COM_PORT> 115200 & sleep 8` |
| 串口日志时效验证 | `stat "$SERIAL_LOG" \| grep Modify` |
| 读取串口日志 | `cat "$SERIAL_LOG"` |
