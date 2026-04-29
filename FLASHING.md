# 烧录手册

STM32F407VET6 主控板的烧录步骤。所有命令默认在仓库根目录 `main-board/` 下执行。
工具链：`openocd 0.12.0` + `arm-none-eabi-gdb`（macOS / Linux）；Windows 备份方案见末尾。

---

## 前置

| 工具 | 检查命令 | 期望 |
|---|---|---|
| openocd | `openocd --version` | 0.12.0 |
| 探针 | 看 USB | `STMicroelectronics STM32 STLink` |
| 产物 | `ls build/bootloader/bootloader/bootloader.hex build/app-a/app/upboard_A.hex build/app-a/app/params_A.bin` | 三文件齐 |

产物缺失先编：

```bash
cmake --preset bootloader && cmake --build --preset bootloader
cmake --preset app-a      && cmake --build --preset app-a
cmake --preset app-b      && cmake --build --preset app-b
```

---

## Flash 布局

| 地址 | 大小 | 内容 |
|---|---|---|
| `0x08000000` | 32 KB | Bootloader |
| `0x08008000` | 16 KB | Boot 参数 A 副本 |
| `0x0800C000` | 16 KB | Boot 参数 B 副本 |
| `0x08020000` | 128 KB | App Slot A |
| `0x08040000` | 128 KB | App Slot B |
| `0x08060000` | 128 KB | 空（备用） |

总占用 384 KB / 512 KB。

---

## 1. 握手（每次先跑这个）

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt; mdw 0xE0042000 1; exit"
```

**通过标志**：日志里出现

```
Info : [stm32f4x.cpu] Cortex-M4 r0p1 processor detected
[stm32f4x.cpu] halted due to debug-request, current mode: Thread
xPSR: 0x01000000 pc: 0x08...
```

出 `Error: init mode failed (unable to connect to the target)` → 跳到末尾【故障排查】，**不要硬跑后面的命令**。

---

## 2. 完整首次 bring-up（一把梭）

mass erase → bootloader → params_A → App A → reset 跑起来。**一行命令**：

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init" -c "reset halt" -c "stm32f4x mass_erase 0" -c "program build/bootloader/bootloader/bootloader.hex verify" -c "program build/app-a/app/params_A.bin 0x08008000 verify" -c "program build/app-a/app/upboard_A.hex verify reset" -c "exit"
```

**通过标志**：三段都看到

```
** Programming Finished **
** Verify Started **
** Verified OK **
```

最后 `** Resetting Target **` → 板子复位运行 App A，**LED2 应闪 2 次**（fallback=1 / A=2 / B=3）。

## 3. 单 image 增量烧录

仅改了 App 代码，不动 bootloader / params：

```bash
# App Slot A
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt" -c "program build/app-a/app/upboard_A.hex verify reset" -c "exit"

# App Slot B
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt" -c "program build/app-b/app/upboard_B.hex verify reset" -c "exit"

# 仅 bootloader
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt" -c "program build/bootloader/bootloader/bootloader.hex verify reset" -c "exit"
```

OpenOCD 的 `program` 会自动按 sector 擦除目标范围，不会动其他 sector。

---

## 4. 切槽位（让 bootloader 跳 A 或 B）

```bash
# 切到 Slot A
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt" -c "program build/app-a/app/params_A.bin 0x08008000 verify reset" -c "exit"

# 切到 Slot B
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt" -c "program build/app-b/app/params_B.bin 0x08008000 verify reset" -c "exit"
```

> `params_*.bin` 由 `tools/make_params.py` 生成,CMake 配置时自动产到对应 build 目录。

---

## 5. 烧后回读校验

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt" -c "mdw 0x08000000 4" -c "mdw 0x08008000 4" -c "mdw 0x08020000 4" -c "mdw 0x08040000 4" -c "exit"
```

**期望值**：

| 地址 | 期望首 4 字 | 含义 |
|---|---|---|
| `0x08000000` | `20020000 080002??` | Bootloader 入口（SP=SRAM 顶 + Reset_Handler thumb 地址） |
| `0x08008000` | `a5a5a5a5 ...` | Boot params A magic |
| `0x08020000` | `20020000 080202??` | App A 入口 |
| `0x08040000` | `20020000 080402??` 或 `ffffffff` | App B 入口（未烧 = 全 FF） |

`Reset_Handler` 末位必须是奇数（thumb 标志位 = 1）。

---

## 6. 读取 / Attach 调试

仅 halt 看寄存器、不烧录：

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; reset halt" -c "reg" -c "exit"
```

GDB attach（VSCode F5 之外，命令行版）：

```bash
# 终端 1：起 OpenOCD GDB server
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

# 终端 2：起 GDB attach 到当前 slot（按需换 elf）
arm-none-eabi-gdb -ex "target extended-remote :3333" -ex "monitor halt" build/app-a/app/upboard_A.elf
```

---

## 故障排查

### A. `init mode failed (unable to connect to the target)`

按概率顺序排：

1. **SWCLK / SWDIO 物理松动**（最常见）
   - 万用表通断档：ST-Link 排针 pin 7 ↔ STM32 PA13 (SWDIO)，pin 9 ↔ PA14 (SWCLK)
   - 任一不通就是这条
2. **目标板没真正上电**
   - 万用表测 STM32 任一 VDD ↔ GND ≥ 3.0 V
   - OpenOCD 报的 `Target voltage: 3.13` 只能证明 ST-Link 1pin 上有电，不代表 LDO 起来了
3. **NRST 被外部拉死**
   - 万用表测 STM32 NRST 引脚对 GND ≥ 2.5 V
   - 低于 1V → 复位按钮卡死或 RC 电路坏
4. **App 关了 SWD（理论上本项目不会）**
   - 把 BOOT0 短到 3.3V **保持住** → 按 RST 释放 → 跑握手命令
   - 进 system memory bootloader 后 PA13/PA14 强制是 SWD，必能连
   - 能连上 → 跑【完整首次 bring-up】整片擦掉旧固件即恢复

### B. `open failed`（探针 USB 句柄异常）

```bash
pkill -9 openocd; sleep 1
# 再跑握手
```

仍不行 → 拔插 USB 线让 macOS 重新枚举。

### C. 烧录中途 `Error: error writing to flash at address ...`

通常是 SWD 速度过高在 program 阶段抖了。降速：

```bash
openocd -f interface/stlink.cfg -c "adapter speed 1000" -f target/stm32f4x.cfg -c "init; reset halt" -c "program build/app-a/app/upboard_A.hex verify reset" -c "exit"
```

### D. `Verified OK` 但板子不跑

- 检查 LED2 闪烁次数：1 = bootloader 仲裁判 fallback（boot params 损坏 / App CRC 错），2 = A 槽 OK，3 = B 槽 OK
- LED2 完全不闪：bootloader 自己卡死，跑【回读校验】看 `0x08000000` 的 vector 是否正确
- LED2 闪 1 次：跑【切槽位】重写 params_A，并确认 App A 入口的 `0x08020000` magic

---

## Windows ST-LINK_CLI 备份方案

Windows 机上仍可用 ST-Link Utility 命令行（README 里的旧流程）：

```cmd
set STLINK="C:\Program Files (x86)\STMicroelectronics\STM32 ST-LINK Utility\ST-LINK Utility\ST-LINK_CLI.exe"

%STLINK% -c SWD UR -ME
%STLINK% -c SWD UR -P build\bootloader\bootloader\bootloader.hex -V after_programming
%STLINK% -c SWD UR -P build\app-a\app\params_A.bin 0x08008000 -V after_programming
%STLINK% -c SWD UR -P build\app-a\app\upboard_A.hex -V after_programming -Rst
```

`-Rst` 烧完复位运行；`UR` = under reset 连接（自带"按住 RST 接管"语义,比 OpenOCD 的 `connect_assert_srst` 稳）。
