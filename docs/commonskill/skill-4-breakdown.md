---
name: task-breakdown
description: "将开发规格拆解为可独立迭代的模块，定义模块间接口契约，为每个模块决策是否需要 mock 测试桩，并显式声明实时性约束。在进入 skill-5 迭代开发之前使用。"
---

# Skill-4: 任务拆解 + Mock 决策

## 触发时机

- 已有开发规格文档（skill-1 输出），准备进入开发
- 新功能模块开发前

---

## 执行步骤

### 第一步：拆分模块

将规格中的功能拆分为独立模块，原则：

- 每个模块有清晰的单一职责
- 模块间通过函数接口或消息队列通信，而非共享全局变量
- 每个模块可以独立验证（有独立的验收条件）

输出模块列表示例：

```
功能：温湿度采集 + UART 上报

模块：
1. bme280_driver    — I2C 读取，输出温湿度原始值
2. sensor_filter    — 滑动平均滤波，输出稳定值
3. report_protocol  — 打包成帧，UART 发送
4. main_scheduler   — 定时触发采集和上报
```

### 第二步：定义接口契约

每对相邻模块之间定义接口，写入代码或注释中，在开发前锁定：

```c
// bme280_driver → sensor_filter
typedef struct {
    float temperature;  // 单位：℃，精度 0.01
    float humidity;     // 单位：%RH，精度 0.1
    uint8_t valid;      // 1=数据有效，0=读取失败
} SensorRawData_t;

HAL_Status bme280_read(SensorRawData_t *out);

// sensor_filter → report_protocol
typedef struct {
    float temp_filtered;
    float humi_filtered;
} SensorData_t;

void filter_update(SensorRawData_t *raw, SensorData_t *out);
```

接口契约锁定后，各模块可以独立开发和测试。

### 第三步：Mock 决策

为每个模块打标签，判断是否需要 mock 测试桩：

**[mock 优先]** — 逻辑复杂、错误路径多、不需要真实硬件验证逻辑正确性

适用场景：
- 协议解析 / 帧封包（UART / MODBUS / 自定义帧）
- I2C / SPI 通信的重试逻辑、超时处理、NACK 处理
- 状态机（多状态跳转、边界条件、异常回退）
- 数据处理（滤波、标定曲线、数据融合）
- OTA 分包逻辑（包序号、重传、完整性校验）
- 错误处理路径（模拟 HAL_TIMEOUT / HAL_ERROR）

**[直接上板]** — 现象即真相，mock 无法验证

适用场景：
- 简单 GPIO 操作（LED、继电器）
- 时钟 / 外设初始化配置（CubeMX 生成部分）
- 中断响应时序（上升沿、优先级抢占）
- DMA 传输（循环模式、半传输中断）
- 所有电气相关（上拉、电平、信号完整性）
- 外设 bring-up 第一步（先确认硬件链路通再谈 mock）

输出示例：

```markdown
| 模块 | Mock 决策 | 原因 |
|------|-----------|------|
| bme280_driver | [直接上板] | 首次 bring-up，先确认硬件链路 |
| sensor_filter | [mock 优先] | 纯算法逻辑，无硬件依赖 |
| report_protocol | [mock 优先] | 帧格式复杂，有 CRC，需测试多种异常帧 |
| main_scheduler | [直接上板] | 依赖 TIM 中断时序，需真实验证定时精度 |
```

### 第四步：实时性声明

对每个涉及中断或时序的模块，显式声明：

```markdown
## 实时性约束

| 模块 | 是否在中断中运行 | 截止时间 | 共享资源 | 保护机制 |
|------|-----------------|----------|----------|----------|
| bme280_driver | 否（主循环轮询） | 100ms 内完成 | 无 | — |
| main_scheduler | 是（TIM3 中断） | 1ms 精度 | g_sensor_data | volatile + 原子读写 |
| report_protocol | 否 | 无硬实时要求 | uart_tx_buf | 禁止在中断中调用 |
```

这张表在 skill-5 迭代中作为"中断安全检查"的参考。

### 第五步：生成 Mock 桩（[mock 优先] 模块）

对标记 [mock 优先] 的模块，生成 `hal_mock.c` 和对应的测试框架：

```c
// hal_mock.c — 只在 PC 测试工程中编译

#include "hal_iface.h"
#include <stdio.h>
#include <string.h>

// ── 调用记录 ──
static int uart_send_count = 0;
static uint8_t uart_last_buf[256];
static uint16_t uart_last_len = 0;

// ── 可注入返回值 ──
HAL_Status mock_uart_next_return = HAL_OK;
HAL_Status mock_i2c_next_return  = HAL_OK;

// ── Mock 实现 ──
HAL_Status hal_uart_send(uint8_t *data, uint16_t len, uint32_t timeout) {
    uart_send_count++;
    memcpy(uart_last_buf, data, len);
    uart_last_len = len;
    return mock_uart_next_return;
}

HAL_Status hal_i2c_read(uint8_t addr, uint8_t reg,
                        uint8_t *buf, uint16_t len) {
    return mock_i2c_next_return;
}

// ── 断言辅助 ──
void mock_assert_uart_called(int n) {
    if (uart_send_count != n)
        printf("FAIL uart_send: expected %d, got %d\n", n, uart_send_count);
    else
        printf("PASS uart_send called %d time(s)\n", n);
}

void mock_reset(void) {
    uart_send_count = 0;
    uart_last_len   = 0;
    mock_uart_next_return = HAL_OK;
    mock_i2c_next_return  = HAL_OK;
}
```

测试用例骨架：

```c
// test_report_protocol.c
// 编译：gcc -o test test_report_protocol.c hal_mock.c report_protocol.c

#include "hal_mock.h"
#include "report_protocol.h"

void test_normal_send(void) {
    mock_reset();
    SensorData_t d = {25.0f, 60.0f};
    report_send(&d);
    mock_assert_uart_called(1);
}

void test_retry_on_timeout(void) {
    mock_reset();
    mock_uart_next_return = HAL_TIMEOUT;
    SensorData_t d = {25.0f, 60.0f};
    report_send(&d);
    mock_assert_uart_called(3);  // 期望重试 3 次
}

int main(void) {
    test_normal_send();
    test_retry_on_timeout();
    return 0;
}
```

### 第六步：确定开发顺序

按以下优先级排序：

1. 风险最高的模块优先（通信 / 实时 / 电源相关）
2. 被其他模块依赖的底层模块优先
3. [mock 优先] 模块可以并行开发，不阻塞上板

---

## 执行规则

- 接口契约必须在开发前锁定，中途修改接口必须通知所有依赖方
- 实时性声明不可跳过，涉及中断的模块必须明确写出共享资源和保护机制
- Mock 决策是工程判断，不是所有模块都需要 mock
- 测试用例必须覆盖至少一条正常路径和一条错误路径
