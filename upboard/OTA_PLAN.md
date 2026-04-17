# STM32F407 OTA 实施 Plan

## 总体目标
在现有工程上加入 A/B 双槽 OTA 能力，保证升级失败可回滚、不变砖；通过现有 ESP（USART2）通道下发固件。

## Flash 分区（按 F407 扇区边界对齐）

| 用途 | 扇区 | 地址 | 大小 | 说明 |
|---|---|---|---|---|
| Bootloader | S0~S1 | 0x08000000 | 32KB | 不可升级，只做判断+跳转 |
| 参数区 A | S2 | 0x08008000 | 16KB | 启动标志/版本/CRC |
| 参数区 B | S3 | 0x0800C000 | 16KB | 参数冗余，防掉电 |
| 保留 | S4 | 0x08010000 | 64KB | 预留（日志/配置/密钥） |
| **App A（Slot 0）** | S5 | 0x08020000 | 128KB | 运行槽 |
| **App B（Slot 1）** | S6 | 0x08040000 | 128KB | 升级槽 |
| 用户数据 | S7 | 0x08060000 | 128KB | 预留 |

App 当前 14KB，128KB 槽位非常充裕。

## 参数区结构

```c
typedef struct {
    uint32_t magic;          // 0xA5A5A5A5
    uint32_t boot_slot;      // 0=A, 1=B
    uint32_t slot_valid[2];  // CRC 通过标志
    uint32_t slot_size[2];
    uint32_t slot_crc32[2];
    uint32_t slot_version[2];
    uint32_t pending_update; // 1=有新固件待切换
    uint32_t boot_count;     // App 启动后未确认的次数
    uint32_t self_crc;
} boot_params_t;
```

A/B 双副本，写入顺序固定（先 B 后 A），任一有效即可启动。

---

## 阶段 1：跑通 "App 在 0x08020000 启动"

**目的**：先用 ST-Link 直接把现有 App 烧到 0x08020000，证明能跑在非零地址。

**改动**：
- `Core/Src/system_stm32f4xx.c`：定义 `USER_VECT_TAB_ADDRESS` + `VECT_TAB_OFFSET = 0x20000`
- `MDK-ARM/upboard.uvprojx`：IROM 起始 0x8020000，大小 0x20000

**验收**：
- [ ] ST-Link 烧到 0x08020000，板子能跑
- [ ] USART2 日志、INA226、风扇、GPIO 行为与之前一致
- [ ] 中断响应正常（验证 VTOR 重定位生效）

---

## 阶段 2：写最小 Bootloader（固定跳 A 槽）

**目的**：Bootloader 上电固定跳到 0x08020000，验证跳转机制。

**新建**：`bootloader/` 独立工程
- `bootloader.sct`：`LR_IROM1 0x08000000 0x00008000`
- 文件：`Core/Src/main.c` `boot_jump.c/h` `boot_uart.c/h`

**核心代码**：
```c
void jump_to_app(uint32_t app_addr) {
    uint32_t msp = *(volatile uint32_t*)app_addr;
    uint32_t pc  = *(volatile uint32_t*)(app_addr + 4);
    HAL_DeInit();
    __disable_irq();
    SCB->VTOR = app_addr;
    __set_MSP(msp);
    ((void(*)(void))pc)();
}
```

**验收**：
- [ ] 烧 Bootloader(0x08000000) + App(0x08020000)，板子能跑
- [ ] Bootloader 串口打印 "Boot v0.1, jumping to A"
- [ ] App 接管后所有功能正常

---

## 阶段 3：参数区 + A/B 槽选择

**新建**（共用模块）：
- `Common/boot_params.c/h`
- `Common/crc32.c/h`

**API**：
- `BootParams_Read(boot_params_t *p)`：读 A 区，CRC 失败读 B 区
- `BootParams_Write(const boot_params_t *p)`：A、B 各写一份
- `BootParams_SelectSlot(const boot_params_t *p)`：返回应启动地址

**验收**：
- [ ] ST-Link 手动改参数区 boot_slot=1，烧 App 到 B 槽（0x08040000），Bootloader 能正确跳过去
- [ ] 拉电源测试参数区掉电保护（A 写完 B 没写完，重启仍能恢复）

---

## 阶段 4：App 端 Flash 写入 + CRC 模块

**新建**：`Core/Src/ota.c` `Core/Inc/ota.h`
- `Ota_Begin(slot)`：擦除目标槽
- `Ota_WriteChunk(offset, data, len)`：按对齐写入
- `Ota_End(total_size)`：算 CRC32，写参数区，置 pending
- `Ota_Verify(slot)`：读回算 CRC

**注意**：
- F407 写 Flash 必须 unlock/lock
- 单次擦 128KB 扇区耗时数百 ms，要喂狗
- 写入期间临时关高优先级中断

**验收**：
- [ ] App 把"自己镜像"复制到另一个槽
- [ ] 复位后 Bootloader 跳到新槽运行，行为完全一致

---

## 阶段 5：ESP 协议扩展（OTA 命令）

**协议扩展**（`esp_comm.h` 加 cmd）：
```
0x30 OTA_BEGIN    payload: total_size(4) + version(4) + crc32(4)
0x31 OTA_DATA     payload: seq(2) + data(N)
0x32 OTA_END      payload: 无
0xB0 OTA_ACK      payload: status(1) + seq(2)
0xB1 OTA_DONE     payload: status(1)
```

**注意**：`ESP_MAX_PAYLOAD` 当前 32 太小，128KB 固件 = 4096 包，建议调到 256 或 512。

**验收**：
- [ ] PC 工具发送 OTA_BEGIN → 多包 OTA_DATA → OTA_END
- [ ] App 接收并写入另一槽，CRC 通过 → 自动复位 → Bootloader 跳新槽

---

## 阶段 6：回滚 + 看门狗

- App 启动 N 秒内调用 `Ota_ConfirmBoot()` 清 boot_count
- Bootloader 启动时若 boot_count > 阈值（如 3），切回旧槽
- 启用 IWDG 独立看门狗

**验收**：
- [ ] 故意烧死锁固件到 B 槽，触发升级
- [ ] 板子复位 3 次后自动回到 A 槽

---

## 阶段 7：上位机工具 + 端到端联调

- `tools/ota_tool.py`：读 hex/bin → 切片 → 走 ESP 协议
- 测试矩阵：正常升级 / 中途断电 / 中途断包 / CRC 错误 / 新固件死锁

---

## 文件清单

**新建**：
```
upboard/
├── bootloader/
│   ├── MDK-ARM/bootloader.uvprojx
│   ├── Core/Src/main.c
│   ├── Core/Src/boot_jump.c
│   └── bootloader.sct
├── Common/
│   ├── boot_params.c/h
│   └── crc32.c/h
├── Core/Src/ota.c
├── Core/Inc/ota.h
└── tools/ota_tool.py
```

**修改**：
- `MDK-ARM/upboard.uvprojx`：IROM 基址 0x08020000
- `Core/Src/system_stm32f4xx.c`：启用 USER_VECT_TAB_ADDRESS
- `Core/Src/main.c`：N 秒后 confirm boot
- `Core/Inc/esp_comm.h`：增 OTA 命令码、调大 payload
- `Core/Src/esp_comm.c`：加 OTA 命令处理

---

## 风险与注意事项

| 风险 | 应对 |
|---|---|
| Bootloader 自身有 bug 变砖 | Bootloader 充分测试后冻结 |
| Flash 写入被中断打断 | 写期间临时关中断 |
| 参数区写一半掉电 | A/B 双副本 + self_crc |
| ESP_MAX_PAYLOAD=32 太慢 | 阶段 5 调到 256 |

---

## 与主线开发的关系

主线在"阶段 4 联调日志与保护逻辑"，OTA 是独立支线。建议主线阶段 4 完成后开始 OTA，或在独立分支并行做阶段 1~2。
