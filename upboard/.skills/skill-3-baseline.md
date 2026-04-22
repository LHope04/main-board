---
name: project-baseline
description: "在 CubeMX 生成代码后或接手陌生工程时，对 upboard 三 image（bootloader / app-a / app-b）的 CMake 配置进行体检：时钟树、引脚冲突、空工程编译、烧录验证、建立 Flash/RAM 基线。确保后续开发建立在干净的基础上。"
---

# Skill-3：工程基线验证（CMake + arm-none-eabi-gcc）

## 触发时机

- CubeMX 生成代码后，开始业务开发之前
- 对一个陌生的已有工程接手开发时
- 升级 HAL / CMSIS 后，确认新基线
- 工具链 / Toolchain file 改动后

---

## 执行步骤

### 第一步：CubeMX 配置审查

对照 `.context/hardware.context.md`，逐项检查 CubeMX 实际生成的配置：

**时钟树检查**
- SYSCLK = 168 MHz？HSE = 8 MHz × PLL（M=8 N=336 P=2）？
- APB1 / APB2 分频是否正确（APB1=42M, APB2=84M）？
- 使用到的外设时钟源是否已使能？

**引脚分配检查**
- 所有外设引脚是否与原理图一致？
- 有没有引脚复用冲突（编译时不报错，运行时异常）？
- USART2 (PD5/PD6) 必须保留给 ESP32 协议，禁止其他用途
- GPIO 模式（输入/输出/复用/模拟）是否正确？

**外设使能检查**
- 用到的外设是否都已在 CubeMX 中使能？
- DMA 通道是否有冲突？
- 中断优先级分组是否设置（NVIC Priority Group）？

发现问题：记录到 `.context/decisions.log.md`，返回 CubeMX 修正，重新生成代码后回到本步。

### 第二步：CMake 配置 + 空工程编译

三个 image 的配置（首次或 CMake 文件改动后必须执行）：

```bash
cmake --preset bootloader
cmake --preset app-a
cmake --preset app-b
```

逐 image 增量编译：

```bash
cmake --build --preset bootloader
cmake --build --preset app-a
cmake --build --preset app-b
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

### 第三步：建立 Flash / RAM 基线

链接结束后，CMake 已经自动 `arm-none-eabi-size` 输出过用量。要拿精确数字，读 MAP 或重新跑 size：

```bash
# 直接看 size（与构建末尾打印一致）
arm-none-eabi-size build/bootloader/bootloader/bootloader.elf
arm-none-eabi-size build/app-a/app/upboard_A.elf
arm-none-eabi-size build/app-b/app/upboard_B.elf

# MAP 精确分段（找最大占用源）
grep -E "^\.text|^\.data|^\.bss|^\._user_heap_stack" build/app-a/app/upboard_A.map | head -20
```

记录到 `.context/project.context.md`：

```markdown
## 资源用量基线（空工程或当前快照，CMake + arm-none-eabi-gcc）
| Image      | Flash 区段        | text  | data | bss   | 占用 / 上限       |
|------------|-------------------|-------|------|-------|-------------------|
| bootloader | 0x08000000 32 KB  | X KB  | Y B  | Z KB  | A KB / 32 KB      |
| app-a      | 0x08020000 128 KB | X KB  | Y B  | Z KB  | A KB / 128 KB     |
| app-b      | 0x08040000 128 KB | X KB  | Y B  | Z KB  | A KB / 128 KB     |

RAM (192 KB SRAM)：app data+bss = X KB，stack 1 KB，heap 0.5 KB
基线建立日期：YYYY-MM-DD
```

这个基线作为后续 skill-5 中 `.map` 预警的对比基准。

### 第四步：空烧录验证（端到端工具链通畅）

第一次 bring-up 严格按下面顺序：

```bash
STLINK="/c/Program Files (x86)/STMicroelectronics/STM32 ST-LINK Utility/ST-LINK Utility/ST-LINK_CLI.exe"

# 1. 整片擦除
"$STLINK" -c SWD UR -ME

# 2. Bootloader（sectors 0-1）
"$STLINK" -c SWD UR -P build/bootloader/bootloader/bootloader.hex -V after_programming

# 3. Boot params A（sector 2，告诉 bootloader 跳 A）
"$STLINK" -c SWD UR -P build/app-a/app/params_A.bin 0x08008000 -V after_programming

# 4. App A（sector 5，最后一步加 -Rst 开始运行）
"$STLINK" -c SWD UR -P build/app-a/app/upboard_A.hex -V after_programming -Rst
```

**通过标准**：每条命令都打印 `Programming Complete` 和 `Verification...OK`。任何一条缺 `Verification...OK` → 停止排查，不继续。

常见问题：
- `No ST-LINK detected` → 检查 USB 线 / 接头 / 目标板供电
- `Can't reset the core` → 改用 HotPlug 模式或断电重连
- 校验失败 → 通常是写保护或 SWD 线干扰；先 `-ME` 再重试
- App 烧完不跑 → 看下一步内存读，确认 bootloader 实际跳了

### 第五步：基础功能冒烟测试

烧录成功后，确认最基础的行为正常：

```bash
# 1. 槽位标记（survives soft reset）：A=0xA0A0A0A0  B=0xB0B0B0B0
"$STLINK" -c SWD HotPlug -r32 0x40002850 1

# 2. 主时钟运行：读 SysTick → 等 1 秒 → 再读，应有变化
"$STLINK" -c SWD HotPlug -r32 0xE000E018 1
sleep 1
"$STLINK" -c SWD HotPlug -r32 0xE000E018 1

# 3. App 入口确认：MAP 找到 main，对比 vector 表 reset handler
grep -E "\bmain\b" build/app-a/app/upboard_A.map | head -3
"$STLINK" -c SWD HotPlug -r32 0x08020004 1   # reset handler 入口（应在 0x0802xxxx 范围）
```

**通过标准**：
- BKP0R = `0xA0A0A0A0`（A 槽）或 `0xB0B0B0B0`（B 槽）
- SysTick 在变化，证明 CPU 在跑
- Reset handler 地址落在 App 槽对应的 sector 内

### 第六步：写入基线记录

在 `.context/project.context.md` 中更新：

```markdown
## 工程基线状态
- 基线建立时间：YYYY-MM-DD
- CubeMX 版本：X.Y.Z
- arm-none-eabi-gcc 版本：10.3.1（chocolatey）
- CMake：3.31.10（vcpkg）
- Ninja：1.13.1
- 三 preset 编译：✅ 0 Error 0 Warning
- 烧录验证：✅（bootloader + params_A + app A 全部 Verification...OK）
- 槽位标记：✅ BKP0R = 0xA0A0A0A0
- Flash 基线：bootloader X KB，app A Y KB，app B Z KB
- RAM 基线：data+bss = X KB
```

---

## 执行规则

- 基线验证必须在添加任何业务代码之前完成。
- 编译有 Warning 不能忽视，必须逐条判断是否是潜在问题。
- 烧录失败不得假设硬件正常，必须先排查工具链和目标连接。
- 烧录步骤之间 **不要 mass-erase**，否则会清掉刚写入的 bootloader / params。
- USART2 是 ESP32 协议端口，bootloader/工具/冒烟测试都不得在上面打明文（违反时改用 LED 或内存读）。
- 基线数据是后续资源管理的唯一参考，必须准确记录到 `.context/project.context.md`。
