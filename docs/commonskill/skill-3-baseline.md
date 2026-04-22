---
name: project-baseline
description: "在 CubeMX 生成代码后或接手陌生工程时，对所有 CMake preset 进行体检：探测 preset 列表、时钟树、引脚冲突、空工程编译、烧录验证、建立 Flash/RAM 基线。确保后续开发建立在干净的基础上。任何 Cortex-M + arm-none-eabi-gcc 工程通用。"
---

# Skill-3：工程基线验证（CMake + arm-none-eabi-gcc，通用）

## 触发时机

- CubeMX / 厂商配置工具生成代码后，开始业务开发之前
- 对一个陌生的已有工程接手开发时
- 升级 HAL / CMSIS 后，确认新基线
- 工具链 / Toolchain file 改动后

---

## Step 0 — 探测工程结构（**第一次进入必跑**）

不假设有几个 preset、image 叫什么、Flash 起始在哪。先发现：

```bash
# 1. 找 CMakePresets.json
test -f CMakePresets.json && echo "found" || echo "no presets — fall back to manual cmake -B"

# 2. 列出所有 configurePreset 名（python 解析最稳）
python -c "
import json, sys
p = json.load(open('CMakePresets.json'))
for x in p.get('configurePresets', []):
    if not x.get('hidden'):
        print(x['name'], '->', x.get('binaryDir', '(default)'))
"

# 3. 找链接脚本（Flash/RAM 起始 + LENGTH 在这里看）
find cmake -name "*.ld" 2>/dev/null
find . -maxdepth 4 -name "*.ld" 2>/dev/null | head -10

# 4. 找工具链文件
find cmake -name "*toolchain*" -o -name "*-gcc.cmake" 2>/dev/null
```

把发现到的内容写入 `.context/project.context.md` 顶部："工程基线状态" 段。后续 skill-3 / skill-5 都按这张表操作。

---

## Step 1 — CubeMX / 配置审查

对照 `.context/hardware.context.md`，逐项检查实际生成的配置：

**时钟树检查**
- SYSCLK 是否达到设计值（通常 = 芯片最高主频）
- HSE 频率与原理图晶振一致
- AHB / APB1 / APB2 分频符合设计
- 用到的外设时钟源已使能

**引脚分配检查**
- 所有外设引脚与原理图一致
- 没有引脚复用冲突（编译不报错，运行时异常）
- **预留给协议 / 不能动的引脚**已标记（典型：BLE / WiFi / 上位机协议串口；具体见 `CLAUDE.md` 的"调试串口规则"段）
- GPIO 模式（输入/输出/复用/模拟）正确

**外设使能检查**
- 用到的外设已在配置工具中使能
- DMA 通道无冲突
- 中断优先级分组已设置（NVIC Priority Group）

发现问题：记录到 `.context/decisions.log.md`，返回 CubeMX 修正，重新生成代码后回到本步。

---

## Step 2 — CMake 配置 + 空工程编译

**对 Step 0 探测到的每个 preset 都跑一遍 configure + build。**

```bash
# 例：PRESETS 替换为 Step 0 探测到的实际列表
PRESETS="<preset-1> <preset-2> ..."

# 首次 / CMake 文件改动后必须先 configure
for p in $PRESETS; do cmake --preset $p; done

# 增量编译
for p in $PRESETS; do cmake --build --preset $p || break; done
```

**通过标准**：每个 preset
- ninja 退出码 0
- 链接阶段输出 `Memory region` 表（RAM / FLASH 用量行）
- 没有 `error:` / `undefined reference:` / `region overflow`

常见问题：
- `cannot find -lxxx` → 检查 `CMakeLists.txt` 的 `target_link_libraries` / 链接器搜索路径
- `region 'FLASH' overflowed` → 链接脚本 LENGTH 不够；先确认是不是少加了 `--gc-sections`
- `multiple definition of 'xxx'` → CMakeLists 双重 `target_sources` 或 HAL 文件重复加入
- `undefined reference to '_sbrk'` 等 newlib 桩 → 工具链文件缺 `--specs=nano.specs --specs=nosys.specs`

---

## Step 3 — 建立 Flash / RAM 基线

链接结束后，CMake 已经自动 `arm-none-eabi-size` 输出过用量。要拿精确数字，读 MAP 或重新跑 size：

```bash
# 直接看 size（与构建末尾打印一致）—— 对每个 preset 的产物 .elf 都跑
arm-none-eabi-size build/<preset>/<sub>/<image>.elf

# MAP 精确分段（找最大占用源）
grep -E "^\.text|^\.data|^\.bss|^\._user_heap_stack" build/<preset>/<sub>/<image>.map | head -20
```

> `<sub>` 是 preset 的 binaryDir 下子目录（如 `app/` `bootloader/`）。Step 0 已经探测到。

记录到 `.context/project.context.md`：

```markdown
## 资源用量基线（空工程或当前快照，CMake + arm-none-eabi-gcc）
| Image      | Flash 区段        | text  | data | bss   | 占用 / 上限       |
|------------|-------------------|-------|------|-------|-------------------|
| <image-1>  | <addr-1> <size-1> | X KB  | Y B  | Z KB  | A KB / <size-1>   |
| <image-2>  | <addr-2> <size-2> | X KB  | Y B  | Z KB  | A KB / <size-2>   |

RAM (<chip RAM>)：data+bss = X KB，stack <S> KB，heap <H> KB
基线建立日期：YYYY-MM-DD
```

这个基线作为后续 skill-5 中 `.map` 预警的对比基准。

---

## Step 4 — 空烧录验证（端到端工具链通畅）

**多 image / OTA 工程**：第一次 bring-up 严格按"低地址先、最后一步加 -Rst"的顺序烧。
**单 image 工程**：直接烧 App。

```bash
STLINK="<STLINK_CLI_PATH>"   # 见 CLAUDE.md

# 0. 整片擦除（首次 bring-up 推荐，之后禁用）
"$STLINK" -c SWD UR -ME

# 多 image 例（按 Flash 地址从低到高，最后一个加 -Rst）：
"$STLINK" -c SWD UR -P build/<preset-bootloader>/<sub>/<image>.hex -V after_programming
"$STLINK" -c SWD UR -P build/<preset-params>/<sub>/<params>.bin <params-addr> -V after_programming
"$STLINK" -c SWD UR -P build/<preset-app>/<sub>/<image>.hex -V after_programming -Rst

# 单 image：
"$STLINK" -c SWD UR -P build/<preset>/<sub>/<image>.hex -V after_programming -Rst
```

**通过标准**：每条命令都打印 `Programming Complete` 和 `Verification...OK`。
任何一条缺 `Verification...OK` → 停止排查，不继续。

常见问题：
- `No ST-LINK detected` → 检查 USB 线 / 接头 / 目标板供电
- `Can't reset the core` → 改用 HotPlug 模式或断电重连
- 校验失败 → 通常是写保护或 SWD 线干扰；先 `-ME` 再重试
- App 烧完不跑 → 看下一步内存读，确认（如有）bootloader 实际跳了

---

## Step 5 — 基础功能冒烟测试

烧录成功后，确认最基础的行为正常：

```bash
# 1. 主时钟在跑：读 SysTick CVR，等 1 秒，再读，应有变化
"$STLINK" -c SWD HotPlug -r32 0xE000E018 1
sleep 1
"$STLINK" -c SWD HotPlug -r32 0xE000E018 1

# 2. 入口确认：MAP 找 main，对比 vector 表 reset handler
grep -E "\bmain\b" build/<preset>/<sub>/<image>.map | head -3
"$STLINK" -c SWD HotPlug -r32 <vector_addr+4> 1
# vector_addr 即该 image 在 Flash 中的起始地址（链接脚本 ORIGIN）
# reset handler 应落在 [vector_addr, vector_addr + image_size) 范围内

# 3. （仅 OTA 工程）槽位标记：地址与含义见 CLAUDE.md <SLOT_MARK>
"$STLINK" -c SWD HotPlug -r32 <slot_mark_addr> 1
```

**通过标准**：
- SysTick CVR 在变化，证明 CPU 在跑
- Reset handler 地址落在目标 image 的 Flash 区间内
- （如有 OTA）槽位标记符合预期值

---

## Step 6 — 写入基线记录

在 `.context/project.context.md` 中更新：

```markdown
## 工程基线状态
- 基线建立时间：YYYY-MM-DD
- 芯片：<CHIP>
- CubeMX 版本：X.Y.Z
- arm-none-eabi-gcc 版本：<version>
- CMake：<version>
- Ninja：<version>
- Preset 列表：<preset-1>, <preset-2>, ...
- 全 preset 编译：✅ 0 Error 0 Warning
- 烧录验证：✅（全部 image Verification...OK）
- （OTA 工程）槽位标记：✅ <slot_mark_addr> = <expected>
- Flash 基线：见上面 "资源用量基线" 表
- RAM 基线：data+bss = X KB
```

---

## 执行规则

- Step 0 探测必须最先跑，禁止凭记忆假设 preset 名 / Flash 地址 / 产物路径。
- 基线验证必须在添加任何业务代码之前完成。
- 编译有 Warning 不能忽视，必须逐条判断是否是潜在问题。
- 烧录失败不得假设硬件正常，必须先排查工具链和目标连接。
- **多 image 烧录步骤之间不要 mass-erase**，否则会清掉刚写入的 bootloader / params。
- **被项目协议占用的 UART**（见 `CLAUDE.md` "调试串口规则"段）禁止打调试明文，违反时改用 LED 或内存读。
- 基线数据是后续资源管理的唯一参考，必须准确记录到 `.context/project.context.md`。
