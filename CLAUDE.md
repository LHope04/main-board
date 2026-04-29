# AGENTS.md
> 每次会话自动加载。所有规则在整个会话中持续有效。

---

## 全局规则

- 未经验证，永远不报告成功
- 每次会话开始必须执行冷启动握手（见下）
- 遇到架构决策时，立即追加到 `.context/decisions.log.md`，不等会话结束
- 报告硬件问题时，必须输出精确测量指引，禁止说"可能是硬件问题"
- 每轮迭代开始前必须声明实验假设和排除目标
- 修改代码前检查 `.context/hardware.context.md`，确认引脚和外设配置

### SWD 调试地址硬规则（务必）

**永远不要把 `arm-none-eabi-nm` 输出的地址当成稳定值跨次重编使用。** 每次新增/修改全局变量,BSS 布局会重排,所有 `.bss` 段地址跟着变。我们已经因此误判过两次：扫描结果"全 0"实际是读了旧地址的零空间。

正确做法（按优先级）：
1. **优先让用户用 VSCode `Watch`** — 永远跟着最新 ELF 符号表,不用手算地址。地址变化对用户透明
2. **如果必须用 SWD `mdw`/`mdb`**：每次读取前都重跑 `arm-none-eabi-nm build/<preset>/app/<elf> | grep <symbol>`,拿到当次编译的地址
3. **结构体字段访问**：用 `offsetof(struct, field)`,不要肉眼数字段偏移（我们因为算错 INA226_Device::voltage_V offset 误判过）
4. **批量读全字段时**：`mdw <base> <sizeof_struct/4>`,把整块 dump 出来按 layout 解码,别一字一字读
5. **报告"读到 0/全 FF"前**：先用 nm 重新核对地址 + 检查变量是否在 ELF 里被 GC 掉（无人引用的 volatile 全局会被链接器清除,需要保证有 read 路径）

---

## 工具链约定

构建系统：**CMake + Ninja + arm-none-eabi-gcc**（Keil MDK 已弃用并删除，仓库内不再保留 `MDK-ARM/`）。Mac/Windows 命令完全一致。

### 三个 image，三个 preset

| Image | Preset | Flash 地址 | 产物目录 |
|---|---|---|---|
| Bootloader | `bootloader` | 0x08000000 (32 KB) | `build/bootloader/bootloader/` |
| App Slot A | `app-a` | 0x08020000 (128 KB) | `build/app-a/app/` |
| App Slot B | `app-b` | 0x08040000 (128 KB) | `build/app-b/app/` |

每个 preset 有独立 `build/<preset>/` 目录，互不干扰。

### 构建命令（PATH 已含 cmake/ninja 时）

```bash
# 单 image：增量编译（ninja 自动判断哪些 .o 要重编）
cmake --build --preset {bootloader|app-a|app-b}

# 共享代码改动（Common/、Core/Src/system_stm32f4xx.c、HAL）影响多个 image 时
for p in bootloader app-a app-b; do cmake --build --preset $p; done

# 首次/CMake 文件改了：
cmake --preset <name>     # configure（cache 没变化时几乎瞬时）
cmake --build --preset <name>
```

**Windows PATH（vcpkg/EIDE/chocolatey 拼装）**：
```
C:/Users/q7591/AppData/Local/vcpkg/downloads/tools/cmake-3.31.10-windows/cmake-3.31.10-windows-x86_64/bin
C:/Users/q7591/AppData/Local/vcpkg/downloads/tools/ninja/1.13.1-windows
```
arm-none-eabi-gcc/openocd 在 `C:\ProgramData\chocolatey\bin\`（已在系统 PATH）。

### 烧录（ST-Link_CLI 仍是首选，最稳）

```bash
STLINK="/c/Program Files (x86)/STMicroelectronics/STM32 ST-LINK Utility/ST-LINK Utility/ST-LINK_CLI.exe"

# Bootloader
"$STLINK" -c SWD UR -P build/bootloader/bootloader/bootloader.hex -V after_programming

# Boot params（写入 0x08008000，告诉 bootloader 跳哪个 slot）
"$STLINK" -c SWD UR -P build/app-a/app/params_A.bin 0x08008000 -V after_programming

# App slot（A 或 B），最后一个步骤加 -Rst 复位运行
"$STLINK" -c SWD UR -P build/app-a/app/upboard_A.hex -V after_programming -Rst
```

### 调试 / 内存读取

- VSCode F5：`.vscode/launch.json` 已配 4 套 Cortex-Debug（Bootloader / App A / App B / Attach），走 OpenOCD + arm-none-eabi-gdb
- 命令行 OpenOCD：`openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt; mdw 0x40002850; exit"`
- ST-Link_CLI 内存读：`"$STLINK" -c SWD HotPlug -r32 <address> 1`（一次读一个字最稳）

### 关键文件

- 编译产物：`build/<preset>/{bootloader|app}/<image>.{elf,hex,bin,map}`
- MAP 文件：`build/<preset>/<sub>/<image>.map`
- 链接脚本：`cmake/ld/STM32F407_{BOOTLOADER,APP_A,APP_B}.ld`
- 工具链文件：`cmake/arm-none-eabi-gcc.cmake`
- Preset 定义：`CMakePresets.json`
- 串口日志：`tools/serial_log.txt`
- 逻辑分析仪导出：`tools/logic_analyzer.csv`（Saleae 解码后 CSV，有则读取）

---

## 上下文文件

| 文件 | 内容 | 读写规则 |
|------|------|----------|
| `.context/project.context.md` | 模块状态 · todo · Flash/RAM 用量 | 每次会话读取，结束时更新 |
| `.context/hardware.context.md` | 引脚表 · 外设配置 · 时钟树 | 每次会话读取，硬件变更时更新 |
| `.context/decisions.log.md` | 架构决策 · 排除方案 | 只追加，不修改历史记录 |

---

## 知识库

路径：`knowledge-base/`（独立仓库，挂载或相对路径引用）

按需检索，匹配当前任务涉及的芯片、外设或问题类型：

| 文件 | 检索时机 |
|------|----------|
| `hal-patterns.md` | 开发外设驱动时 |
| `known-bugs.md` | 遇到外设异常 · 初始化问题时 |
| `rtos-patterns.md` | 涉及任务/队列/信号量时 |
| `debug-recipes.md` | 进入调试排查阶段时 |

---

## 冷启动握手（每次新会话必须执行）

```
1. 读取 .context/ 下三个文件
2. 用一段话复述：当前项目是什么 · 哪些模块已完成 · 哪些进行中 · 遗留问题
3. 确认本次任务目标
4. 声明将使用哪个 skill · 完成后更新哪些文件
```

复述示例：
> "当前项目 upboard，UART 驱动和 BME280 驱动已验证，state_machine 开发中（ERROR→IDLE 回退逻辑未完成），Flash 用量 47KB/128KB。本次任务：完成状态机回退逻辑，使用 skill-5，完成后更新 project.context.md。"

---

## Skill 索引与选择规则

| 场景 | 执行路径 |
|------|----------|
| 全新项目启动 | skill-0 → skill-1 → skill-2 → skill-3 → skill-4 → skill-5 |
| 已有项目，开发新功能 | skill-0 → skill-4 → skill-5 |
| 遇到硬件相关异常 | skill-2（重新检索）+ knowledge-base |
| 当前功能调试迭代中 | skill-5 |
| 所有功能完成 | skill-6 |
| 任意时刻换新终端 | skill-0 冷启动，然后继续上次 skill |

---

## .skills/ 文件列表

- `.skills/skill-0-memory.md`
- `.skills/skill-1-requirements.md`
- `.skills/skill-2-hardware.md`
- `.skills/skill-3-baseline.md`
- `.skills/skill-4-breakdown.md`
- `.skills/skill-5-iterative-dev.md`
- `.skills/skill-6-integration.md`
