---
name: migrate-keil-to-cmake
description: "把 Keil MDK 工程（.uvprojx + Core/Src/Inc + Drivers/HAL + scatter 文件）迁移到 CMake + Ninja + arm-none-eabi-gcc + ST-Link/OpenOCD 架构。覆盖：解析 Keil 工程参数 → 生成工具链文件 → 重写链接脚本 → 切换启动文件 → 写 CMakeLists/Presets → 配 VSCode 调试 → 验证编译/烧录 → 清理 Keil 残留。任何 STM32 / Cortex-M + CubeMX 工程通用。用户说迁移到 CMake / 摆脱 Keil / 转 GCC 工程 时进入。"
---

# Skill: Keil → CMake/GCC/OpenOCD 工程迁移

## 触发时机

- 用户希望摆脱 Keil MDK，改用 CMake + Ninja + arm-none-eabi-gcc 工具链
- 跨平台需求（Mac/Linux 协作，Keil 只有 Windows）
- 想用 VSCode Cortex-Debug + OpenOCD 替代 µVision 调试器
- 想接入 CI（Keil 命令行 license 难处理）

---

## 主流程

```
Step 0 探测       → Step 1 解析 Keil → Step 2 决定 CMake 拓扑
   ↓
Step 3 工具链文件 → Step 4 链接脚本   → Step 5 启动文件切换
   ↓
Step 6 写 CMakeLists → Step 7 写 Presets → Step 8 配 VSCode 调试
   ↓
Step 9 编译验证   → Step 10 烧录验证   → Step 11 清理 Keil 残留
   ↓
Step 12 目录角色规整（可选）
```

每步完成即跑 `cmake --build`，**绝不积攒错误到最后一起处理**。

---

## Step 0 — 工程探测（**第一步必跑**）

不假设 Keil 工程结构，先发现：

```bash
# Keil 工程入口
find . -maxdepth 4 -name "*.uvprojx" -o -name "*.uvoptx" -o -name "*.uvgu*"

# CubeMX 工件
find . -maxdepth 2 -name "*.ioc" -o -name ".mxproject"

# HAL / CMSIS 位置
find Drivers -maxdepth 3 -type d 2>/dev/null

# 启动文件（注意 arm/ 是 Keil 用，gcc/ 是我们要用）
find Drivers -name "startup_*.s" 2>/dev/null

# Keil 输出目录（迁移完要删）
find . -maxdepth 3 -type d \( -name "Listings" -o -name "Objects" -o -name "RTE" -o -name "MDK-ARM" \) 2>/dev/null

# 现有 scatter 文件（Keil 自定义内存布局）
find . -name "*.sct" 2>/dev/null
```

把发现到的内容列成一张清单，决定 Step 1 之后的输入材料。

---

## Step 1 — 解析 Keil 工程关键参数

`.uvprojx` 是 XML，用 grep / Python 抽以下字段：

```bash
UVPROJX=$(find . -name "*.uvprojx" | head -1)

# 芯片型号
grep -E "<Device>" "$UVPROJX" | head -1

# Flash / RAM 起始 + 大小（IROM1 / IRAM1）
grep -E "OCR_RVCT[1-9]|IROM[1-9]|IRAM[1-9]" "$UVPROJX" | head -10

# 编译宏（USE_HAL_DRIVER, STM32F4xxxx 等）
grep -E "<Define>" "$UVPROJX"

# 包含路径
grep -E "<IncludePath>" "$UVPROJX"

# 链接的源文件清单
grep -E "<FilePath>.*\.(c|s|S)</FilePath>" "$UVPROJX"

# 启动文件（Keil 通常引用 arm/startup_*.s）
grep -E "startup_.*\.s" "$UVPROJX"

# scatter 文件（如果改过默认）
grep -E "ScatterFile" "$UVPROJX"

# 优化等级（O0/O1/Os 等）
grep -E "<Optim>" "$UVPROJX"
```

整理成一张参数表，作为 Step 3-7 的输入：

```markdown
| 项 | 值 |
|---|---|
| 芯片 | <例如 STM32F407VGT6> |
| Cortex 核 | <例如 Cortex-M4F，FPU sp> |
| Flash 起始 | 0x08000000 |
| Flash 大小 | 1024 KB |
| RAM 起始 | 0x20000000 |
| RAM 大小 | 192 KB（128 + 64） |
| 编译宏 | USE_HAL_DRIVER, STM32F407xx |
| HAL 源文件数 | <例如 21 个> |
| 启动文件 | startup_stm32f407xx.s（要切到 gcc 版） |
| scatter | <自定义 / 默认> |
```

---

## Step 2 — 决定 CMake 拓扑

| 工程类型 | CMake 结构 |
|---|---|
| 单 image（无 OTA） | 顶层 `CMakeLists.txt` 直接 `add_executable(<name>.elf ...)`，单 preset |
| 多 image（OTA：bootloader + app A/B） | 顶层用 `<TARGET>` cache var 选 `add_subdirectory(<image>)`，每 image 一个 preset |
| 多板共代码（变体编译） | 用 cache var 选不同链接脚本 + 编译宏 |

**OTA 工程拓扑模板**（参考 upboard 已落地）：

```
project_root/
├── CMakeLists.txt              # 顶层，按 cache var 拉子目录
├── CMakePresets.json           # bootloader / app-a / app-b
├── bootloader/
│   ├── CMakeLists.txt
│   └── Src/main.c
├── app/
│   ├── CMakeLists.txt          # 同一份源码，靠 SLOT cache var 选不同 .ld
│   ├── Src/                    # 业务代码
│   └── Inc/
├── Core/                       # CubeMX 自动生成（不动）
├── Common/                     # bootloader 与 app 共享代码
├── Drivers/                    # vendor HAL/CMSIS
└── cmake/
    ├── arm-none-eabi-gcc.cmake # 工具链
    └── ld/                     # 链接脚本，每 image 一份
```

---

## Step 3 — 生成 cmake/arm-none-eabi-gcc.cmake

通用 Cortex-M4F 模板（按 Step 1 的核换 `-mcpu` `-mfpu`）：

```cmake
# Toolchain file for arm-none-eabi-gcc, Cortex-M4F (sp FPU).
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

# 不让 CMake 在交叉编译时跑 try_compile（会失败因为没有 main()）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 核相关：按 Step 1 探测结果替换
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(COMMON_FLAGS "${CPU_FLAGS} -Wall -Wextra -ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS_INIT   "${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS} -fno-rtti -fno-exceptions")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${CPU_FLAGS} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage")
```

**核映射对照**：

| 芯片家族 | -mcpu | -mfpu | -mfloat-abi |
|---|---|---|---|
| Cortex-M0/M0+ | cortex-m0 / cortex-m0plus | （无） | soft |
| Cortex-M3 | cortex-m3 | （无） | soft |
| Cortex-M4F (sp) | cortex-m4 | fpv4-sp-d16 | hard |
| Cortex-M7 (sp) | cortex-m7 | fpv5-sp-d16 | hard |
| Cortex-M7 (dp) | cortex-m7 | fpv5-d16 | hard |
| Cortex-M33 (sp) | cortex-m33 | fpv5-sp-d16 | hard |

---

## Step 4 — 重写链接脚本（GNU ld 语法）

**Keil 的 `.sct` scatter 不能直接给 GCC**，必须用 GNU ld 语法重写。一份 `.ld` 模板：

```ld
/* cmake/ld/<chip>_<image>.ld */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K   /* 按 image 改 */
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
    CCM   (xrw) : ORIGIN = 0x10000000, LENGTH = 64K     /* F4 系列才有 */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH
    .text       : { . = ALIGN(4); *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
                    KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } >FLASH
    .rodata     : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } >FLASH
    .ARM.extab  : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM        : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH

    .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*))
                       PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
    .init_array    : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*)))
                       KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
    .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*)))
                       KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH

    _sidata = LOADADDR(.data);
    .data   : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } >RAM AT>FLASH
    .bss    : { . = ALIGN(4); _sbss = __bss_start__ = .; *(.bss) *(.bss*) *(COMMON)
                . = ALIGN(4); _ebss = __bss_end__ = .; } >RAM
    ._user_heap_stack : { . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .);
                          . = . + _Min_Heap_Size; . = . + _Min_Stack_Size; . = ALIGN(8); } >RAM

    /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
```

**多 image / OTA 工程**：每 image 一份，差异只在 `MEMORY { FLASH }` 的 `ORIGIN` + `LENGTH`。

**关键点**：
- `KEEP(*(.isr_vector))` 不能漏 —— 否则 `--gc-sections` 会丢掉中断向量表
- `.data` 用 `>RAM AT>FLASH` 双段：装载在 Flash、运行时在 RAM，由 startup 拷贝
- `_estack` 必须等于 RAM 顶 —— Cortex-M 上电从 `0x08000000` 取的就是这个值
- F4 的 CCM (0x10000000) 与主 RAM 不连续，要不要用看应用

---

## Step 5 — 切换启动文件

Keil 用的是 ARM 汇编语法的 startup（`Drivers/CMSIS/.../Source/Templates/arm/startup_stm32xxxx.s`），GCC 用不了。
**vendor 包里同位置的 `gcc/` 子目录**有 GAS 语法版本，直接换：

```
# Keil 用：
Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/arm/startup_stm32f407xx.s

# 改成：
Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f407xx.s
```

如果 vendor 包没带 gcc 版（极少见），从 ST 官方仓库拉一份。**不要试图把 arm 版手翻成 GAS** —— 太多寄存器汇编差异。

---

## Step 6 — 写 CMakeLists.txt（多 image / OTA 模板）

**顶层 `CMakeLists.txt`**：

```cmake
cmake_minimum_required(VERSION 3.22)
project(<project_name> C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(<TARGET> "" CACHE STRING "Which firmware image to build: bootloader | app-a | app-b")

if(<TARGET> STREQUAL "bootloader")
    add_subdirectory(bootloader)
elseif(<TARGET> STREQUAL "app-a")
    set(<SLOT> "A")
    add_subdirectory(app)
elseif(<TARGET> STREQUAL "app-b")
    set(<SLOT> "B")
    add_subdirectory(app)
else()
    message(FATAL_ERROR "<TARGET> not set. Use cmake --preset {bootloader|app-a|app-b}.")
endif()
```

**每个子目录 `CMakeLists.txt`**（以 app 为例）：

```cmake
set(TARGET upboard_${<SLOT>})
set(ELF   ${TARGET}.elf)
set(REPO_ROOT ${CMAKE_SOURCE_DIR})

if(<SLOT> STREQUAL "A")
    set(LINKER_SCRIPT ${REPO_ROOT}/cmake/ld/<chip>_APP_A.ld)
    set(VECT_TAB_OFFSET 0x00020000U)
else()
    set(LINKER_SCRIPT ${REPO_ROOT}/cmake/ld/<chip>_APP_B.ld)
    set(VECT_TAB_OFFSET 0x00040000U)
endif()

set(STARTUP_FILE ${REPO_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_<chip>.s)

# HAL 源文件清单（按 Step 1 的 <FilePath> 列表回填，但只列 App 真正用的）
set(HAL_SOURCES
    ${REPO_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    ${REPO_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    # ... 按需添加
)

# 业务源文件
set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
    # ... 按需添加
)

add_executable(${ELF} ${STARTUP_FILE} ${APP_SOURCES} ${HAL_SOURCES})

target_compile_definitions(${ELF} PRIVATE USE_HAL_DRIVER STM32F407xx VECT_TAB_OFFSET=${VECT_TAB_OFFSET})

target_include_directories(${ELF} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${REPO_ROOT}/Core/Inc
    ${REPO_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${REPO_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${REPO_ROOT}/Drivers/CMSIS/Include)

target_compile_options(${ELF} PRIVATE
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<CONFIG:Release>:-Os -g>)

target_link_options(${ELF} PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.map,--cref
    -Wl,--print-memory-usage)

set_target_properties(${ELF} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# 生成 .hex（烧录）+ .bin（OTA / CRC32）
add_custom_command(TARGET ${ELF} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${ELF}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${ELF}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${ELF}>)
```

---

## Step 7 — 写 CMakePresets.json

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 22, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "toolchainFile": "${sourceDir}/cmake/arm-none-eabi-gcc.cmake",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    },
    {
      "name": "bootloader",
      "inherits": "base",
      "cacheVariables": { "<TARGET>": "bootloader" }
    },
    {
      "name": "app-a",
      "inherits": "base",
      "cacheVariables": { "<TARGET>": "app-a" }
    },
    {
      "name": "app-b",
      "inherits": "base",
      "cacheVariables": { "<TARGET>": "app-b" }
    },
    {
      "name": "app-a-debug",
      "inherits": "app-a",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    }
  ],
  "buildPresets": [
    { "name": "bootloader",  "configurePreset": "bootloader" },
    { "name": "app-a",       "configurePreset": "app-a" },
    { "name": "app-b",       "configurePreset": "app-b" },
    { "name": "app-a-debug", "configurePreset": "app-a-debug" }
  ]
}
```

单 image 工程把 `bootloader` / `app-b` 行删掉即可。

---

## Step 8 — 配 VSCode 调试（Cortex-Debug + OpenOCD）

`.vscode/launch.json`：每个 image 一个配置（OTA 工程示例）：

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug Bootloader",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "openocd",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/build/bootloader/bootloader/bootloader.elf",
      "configFiles": ["interface/stlink.cfg", "target/stm32f4x.cfg"],
      "preLaunchTask": "build-bootloader"
    },
    {
      "name": "Debug App A",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "openocd",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/build/app-a/app/upboard_A.elf",
      "configFiles": ["interface/stlink.cfg", "target/stm32f4x.cfg"],
      "preLaunchTask": "build-app-a"
    },
    {
      "name": "Attach (no flash)",
      "type": "cortex-debug",
      "request": "attach",
      "servertype": "openocd",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/build/app-a/app/upboard_A.elf",
      "configFiles": ["interface/stlink.cfg", "target/stm32f4x.cfg"]
    }
  ]
}
```

`.vscode/tasks.json` 配 `build-bootloader` 等 preLaunchTask，调 `cmake --build --preset <name>`。

`target/<chip-family>.cfg` 按芯片选：F0/F1/F3/F4/F7/H7/L0/L1/L4 ...

---

## Step 9 — 编译验证

每写一步都跑一次。完整三 preset 全编：

```bash
PRESETS="bootloader app-a app-b"
for p in $PRESETS; do cmake --preset $p; done
for p in $PRESETS; do cmake --build --preset $p || break; done
```

**通过标准**：
- ninja 退出码 0
- 链接阶段输出 `Memory region Used Size` 表
- 没有 `error:` / `undefined reference:` / `region overflow`

**常见迁移阶段错误**：

| 报错 | 原因 | 修法 |
|---|---|---|
| `undefined reference to 'Reset_Handler'` | 启动文件没加进 add_executable | 加 `${STARTUP_FILE}` |
| `region 'FLASH' overflowed` | 链接脚本 LENGTH 写小了，或 vector 没在 FLASH 段 | 检查 `MEMORY` 与 `.isr_vector >FLASH` |
| `undefined reference to '_sbrk' / '_close' / '_write'` | 缺 `--specs=nosys.specs` | 加到工具链文件的 linker flags |
| `multiple definition of 'main'` | HAL 源文件清单和 App 源文件清单重复 | 去重 |
| 编译过但跑飞 / HardFault | `_estack` 错或 vector 不在 0x08000000 | 检查 `.isr_vector` 是 SECTIONS 第一项 |
| 编译过但 `.data` 全 0 | startup 没拷 .data，或 `_sidata`/`_sdata`/`_edata` 名字与 startup 不一致 | 对照 startup_*.s 里的符号名调链接脚本 |

---

## Step 10 — 烧录验证

```bash
STLINK="<STLINK_CLI_PATH>"

# 全擦
"$STLINK" -c SWD UR -ME

# 烧（多 image 工程从低地址往高地址烧，最后一步加 -Rst）
"$STLINK" -c SWD UR -P build/bootloader/bootloader/bootloader.hex -V after_programming
"$STLINK" -c SWD UR -P build/app-a/app/<image>.hex -V after_programming -Rst
```

**通过标准**：每条都打 `Programming Complete` + `Verification...OK`。

```bash
# 复位后冒烟：SysTick 在跑
"$STLINK" -c SWD HotPlug -r32 0xE000E018 1
sleep 1
"$STLINK" -c SWD HotPlug -r32 0xE000E018 1
# 两次值不同 → CPU 跑起来了
```

或用 OpenOCD：

```bash
openocd -f interface/stlink.cfg -f target/<family>.cfg \
        -c "program build/<preset>/<sub>/<image>.elf verify reset exit"
```

---

## Step 11 — 清理 Keil 残留

确认编译 + 烧录都通过后才动这一步：

```bash
# Keil 工程文件
rm -f *.uvprojx *.uvoptx *.uvgu*

# CubeMX 给 Keil 生成的副产物（CMake 不读）
rm -f .mxproject

# Keil 中间产物目录
rm -rf MDK-ARM/ Listings/ Objects/ RTE/

# 旧 scatter（保留留作参考也行）
rm -f *.sct
```

如果 IDE 句柄锁住空目录（Windows 常见），留着不影响 —— IDE 重启后会消失。

写 `.gitignore`：

```gitignore
# Build artifacts
build/
*.elf *.hex *.bin *.map *.lst *.o *.d
!cmake/ld/*.ld

# Keil residue
.mxproject
*.uvprojx *.uvoptx *.uvgu*
MDK-ARM/ Listings/ Objects/ RTE/

# IDE
.vs/ .idea/ *.swp *~ .DS_Store Thumbs.db

# Python
__pycache__/ *.pyc .venv/
```

---

## Step 12 — 目录角色规整（可选 / 推荐）

CubeMX 默认把所有代码堆 `Core/Src` `Core/Inc`。CMake 改造后建议把**手写业务代码**搬到 `app/Src` `app/Inc`，让目录名 = 内容职责：

```
Core/   ← 仅 CubeMX 自动生成（main / gpio / i2c / usart / stm32f4xx_it / _hal_msp / _hal_conf / system_*）
app/    ← 手写业务模块（驱动 / 协议 / 状态机 / OTA / ...）
Common/ ← bootloader 与 app 共享代码
```

操作：

```bash
mkdir -p app/Src app/Inc
# 把每个手写模块挪过去（按实际文件名）
for m in <module1> <module2> ...; do
    mv "Core/Src/$m.c" "app/Src/$m.c"
    mv "Core/Inc/$m.h" "app/Inc/$m.h"
done
```

更新 `app/CMakeLists.txt` 的源文件路径与 `target_include_directories`，全编验证一次。

**注意**：CubeMX 重新生成代码时**不会**触碰 `app/` 下的文件（它只认识 `.ioc` 里登记的外设代码，会写到 `Core/`）—— 所以这个搬动是安全的。

---

## Step 13 — 写根 README + 同步约定

新建给人看的 `README.md`（与 AI 用的 `CLAUDE.md` 区分）：

- 工程概述 + 目录树
- 工具链表（cmake/ninja/gcc/openocd 版本）
- 构建 / 烧录 / 调试三段命令
- Flash 布局（多 image / OTA 工程必写）
- 关键约定（哪些 UART 不能调试 / printf 怎么用 / Core 是 CubeMX 区域 / ...）

参考 `commonskill/CLAUDE.md.template` 与 `commonskill/README.md`。

---

## 执行规则

- **每写一步立即 `cmake --build`**，不积攒错误
- **不动 `Core/` 里 CubeMX 生成的文件**（保持 `.ioc` 重新生成兼容性）
- **不动 `Drivers/` 里 vendor 代码**（除非要换 HAL 分支）
- **启动文件用 vendor 包里的 `gcc/` 子目录**，不要把 `arm/` 版手翻成 GAS
- **Keil scatter 不能直接给 GCC**，必须重写为 GNU ld 语法
- **链接脚本里 `.isr_vector` 必须是 SECTIONS 第一项 + `KEEP`**，否则 vector 表会被 GC 掉
- **清理 Keil 残留放在 Step 11**，编译/烧录全部通过之前不删任何 Keil 文件（保留回滚路径）
- 多 image 烧录顺序：从低地址到高地址，**只在最后一个 image 加 `-Rst`**
- 迁移期间 `git commit` 的粒度尽量细（每 Step 一个 commit），方便事后定位 regression

---

## 与其他 skill 的关系

- 迁移完成后跑 **skill-3** 建立基线（Flash/RAM 用量记录到 `.context/project.context.md`）
- 后续开发用 **skill-4 → skill-5** 主流程
- 迁移过程中遇到的坑（启动文件差异、scatter 语法等）写回 `knowledge-base/known-bugs.md`
