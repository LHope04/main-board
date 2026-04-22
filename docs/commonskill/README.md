# commonskill — Cortex-M 项目通用 skill 模板

> 适用：任何使用 **CMake + Ninja + arm-none-eabi-gcc + ST-Link / OpenOCD** 的 Cortex-M（STM32 / GD32 / NXP / Nordic / etc.）裸机或 RTOS 项目。
> 不绑定任何具体型号、preset 名、Flash 布局。

---

## 这套 skill 解决什么问题

让 Claude 接手一个新嵌入式工程时拥有可复用的工作流：

- **冷启动握手**（skill-0）—— 每次新会话先读 `.context/`，复述项目状态，避免无脑重新开荒
- **需求结构化**（skill-1）—— 强制定义可测试的功能验收 + 嵌入式特有的可靠性验收
- **硬件上下文感知**（skill-2）—— 从原理图/数据手册/CubeMX 提取硬件配置写入 `.context/hardware.context.md`
- **工程基线验证**（skill-3）—— preset 探测、空工程编译、烧录验证、Flash/RAM 基线
- **任务拆解 + Mock 决策**（skill-4）—— 模块拆分、接口契约、mock 优先 vs 直接上板的判断
- **迭代开发主循环**（skill-5）—— 假设-验证-排除-记录的硬循环，软硬分离 + 硬件移交协议
- **集成验收 + 知识沉淀**（skill-6）—— 可靠性验收、栈深度、写回 knowledge-base

---

## 接入新项目的步骤

### 1. 拷贝目录

把整个 `commonskill/` 目录复制到新项目根，重命名为 `.skills/`（保持隐藏，避免编辑器折叠）：

```bash
cp -r commonskill/ <new-project>/.skills/
cd <new-project>
```

### 2. 准备 CLAUDE.md

把 `CLAUDE.md.template` 复制为根目录 `CLAUDE.md`，填空里的 6 处占位：

```bash
cp .skills/CLAUDE.md.template CLAUDE.md
```

模板里的 `<占位>` 项：

| 占位 | 含义 | 例 |
|---|---|---|
| `<CHIP>` | 芯片型号 | `STM32F407VGT6` |
| `<TOOLCHAIN_PATHS>` | cmake/ninja/gcc/openocd 在本机的路径 | 见 memory `reference_cmake_toolchain_paths` |
| `<STLINK_CLI_PATH>` | ST-LINK_CLI.exe 全路径（Windows） | `/c/Program Files (x86)/.../ST-LINK_CLI.exe` |
| `<PRESET_TABLE>` | 工程的 preset / Flash 地址 / 产物路径表 | 详见模板 |
| `<DEBUG_UART_RULES>` | 调试串口约束（哪些 UART 不能用） | 例：USART2 归 ESP32 |
| `<SLOT_MARK>` | 如果有 OTA，槽位标记的存放位置 | 例：`RTC->BKP0R = 0x40002850` |

没有 OTA / 单 image 工程：把 `<SLOT_MARK>` 整段删掉，把 `<PRESET_TABLE>` 改成单行。

### 3. 准备 .context/

首次进入项目时让 Claude 跑 skill-0，它会按模板创建 `.context/` 三件套：

- `project.context.md` —— 模块状态、Flash/RAM 用量、TODO、已知问题
- `hardware.context.md` —— 引脚/外设/时钟树（skill-2 填）
- `decisions.log.md` —— 架构决策（追加，不改历史）

### 4. 挂载 knowledge-base（可选但强烈推荐）

`knowledge-base/` 是跨项目共享的经验库，建议 git submodule 或软链接：

```
knowledge-base/
├── hal-patterns.md      # 外设驱动通用套路
├── known-bugs.md        # 芯片/HAL 已知 bug
├── debug-recipes.md     # 故障排查路径
└── rtos-patterns.md     # RTOS 使用模式
```

skill-2 / skill-5 / skill-6 会主动检索 + 写回。

---

## 文件清单

### 主流程 skill（按序执行）

| 文件 | 通用度 | 说明 |
|---|---|---|
| `skill-0-memory.md` | ✅ 完全通用 | 冷启动握手 + 会话写回 |
| `skill-1-requirements.md` | ✅ 完全通用 | 需求拆解 + 验收标准 |
| `skill-2-hardware.md` | ✅ 完全通用 | 硬件上下文感知 |
| `skill-3-baseline.md` | ✅ Step-0 探测 preset | 空工程编译 + 基线 |
| `skill-4-breakdown.md` | ✅ 完全通用 | 模块拆解 + mock 决策 |
| `skill-5-iterative-dev.md` | ✅ Step-0 探测 preset | 迭代开发主循环 |
| `skill-6-integration.md` | ✅ 完全通用 | 集成验收 + 知识沉淀 |

### 工具型 skill（按需触发）

| 文件 | 触发场景 | 说明 |
|---|---|---|
| `skill-migrate-keil-to-cmake.md` | 迁移到 CMake / 摆脱 Keil / 转 GCC 工程 | Keil MDK → CMake+Ninja+arm-none-eabi-gcc+ST-Link/OpenOCD 全流程 |

### 模板

| 文件 | 说明 |
|---|---|
| `CLAUDE.md.template` | 项目根 CLAUDE.md 模板（占位待填） |

---

## 与 upboard 项目 `.skills/` 的关系

`upboard/.skills/` 是这套通用模板的"项目特化版本"：

- skill-3 / skill-5 把 preset 列表、Flash 地址、USART2 规则**硬编码**进表格，省去每次探测
- 其他 skill 与本目录基本一致

接入新项目时：先用 `commonskill/` 跑通流程，跑顺以后再像 upboard 那样把项目固有的常量直接固化到 skill 里，加速后续会话。

---

## 工具链假设

- **构建**：CMake ≥ 3.20，Ninja
- **编译器**：arm-none-eabi-gcc（chocolatey / vcpkg / 自装均可）
- **烧录**：ST-LINK_CLI 或 OpenOCD（推荐 ST-LINK_CLI 做盲烧 + 内存读，OpenOCD 做调试）
- **调试**：VSCode + Cortex-Debug（marus25）+ OpenOCD
- **shell**：bash（Windows 用 git-bash / msys2）

如果用 IAR / Keil / Makefile，仅需修改 skill-3 / skill-5 中的构建命令，其他 skill 完全可复用。
