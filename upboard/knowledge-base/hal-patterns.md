# HAL Patterns
> 跨项目通用的外设驱动开发套路与编程模式。
> 开发新外设前先检索是否有匹配的 pattern，直接复用。只追加，不修改历史。

---

## [UART] printf 无输出 — newlib 未 retarget（GCC）/ MicroLib 未开启（Keil）

- **症状**：调用 `printf` 没有任何串口输出；或程序卡死在 `printf`；或链接报 `undefined reference to '_write'` / `'_sbrk'`
- **根因**：
  - **arm-none-eabi-gcc + newlib-nano**：默认 `_write` 走 semihosting，没有调试器接管时直接挂起或丢字。工具链文件需要 `--specs=nano.specs --specs=nosys.specs` 才能链通，但 `nosys` 把 syscalls 全部 stub 成失败 —— `printf` 还是没输出
  - **Keil + 默认 C 库**：不启用 MicroLib 时，`fputc` retargeting 不生效，`printf` 输出到 semihosting（无串口）或挂起
- **解决方案**：

```c
// 方案 A（推荐，跨工具链）：完全绕开 printf
//   不依赖 newlib / MicroLib，行为可预测，无堆分配
char buf[128];
snprintf(buf, sizeof(buf), "temp=%.2f\r\n", temperature);
uart_send_string(buf);   // 项目自己的 UART 发送函数

// 方案 B（GCC newlib retarget）：在 syscalls.c 里实现 _write
//   只要 link 时 specs=nano.specs + nosys.specs 在前、syscalls.o 在后即可覆盖
int _write(int fd, char *ptr, int len) {
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}
// 还需自己实现 _sbrk（heap）才能用 malloc/printf 的浮点；不用浮点格式时可省

// 方案 C（Keil MicroLib）：勾选 Use MicroLIB + 重定 fputc
int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
```

- **注意**：
  - 方案 A 最稳，零运行时依赖，推荐作为嵌入式项目默认。本项目 (upboard) 已迁到 GCC，App 端走方案 A
  - GCC 下用 `printf("%f", ...)` 还需要工具链文件加 `-u _printf_float`（或换非浮点版 snprintf 自己拆数）
  - Bootloader 严格不允许任何 stdio —— 体积小，且 USART2 归 ESP32（见 `USART2 归 ESP32 使用` memory）
- **检索关键词**：printf · 无输出 · newlib · _write · _sbrk · MicroLib · semihosting · uart
- **来源项目**：upboard · 日期：2026-04（GCC 迁移补充）

---

## [TIM] PWM + 输入捕获（IC）混用同一定时器

- **症状**：IC 从不触发，或配置 IC 后 PWM 停止工作
- **根因**：初始化顺序错误。`HAL_TIM_IC_Init` 会重新初始化定时器基础配置，覆盖已有 PWM 设置；`HAL_TIM_IC_Start_IT` 不自动使能溢出中断
- **解决方案**：
```c
// 正确顺序：
// 1. 先初始化 PWM（初始化定时器 base）
HAL_TIM_PWM_Init(&htimx);
HAL_TIM_PWM_ConfigChannel(&htimx, &pwm_config, TIM_CHANNEL_1);
HAL_TIM_PWM_Start(&htimx, TIM_CHANNEL_1);

// 2. 再配置 IC 通道（不要再调用 HAL_TIM_IC_Init，避免重置 base）
HAL_TIM_IC_ConfigChannel(&htimx, &ic_config, TIM_CHANNEL_2);
HAL_TIM_IC_Start_IT(&htimx, TIM_CHANNEL_2);

// 3. IC_Start_IT 不使能溢出中断，需手动开启
__HAL_TIM_ENABLE_IT(&htimx, TIM_IT_UPDATE);
```
- **检索关键词**：PWM · IC · 输入捕获 · 混用 · 同一定时器 · 不触发
- **来源项目**：upboard · 日期：—

---

## [中断安全] volatile 变量快照模式

- **症状**：从多个 volatile 变量计算出的结果（如频率 = 计数差 / 时间差）偶发出现明显错误值
- **根因**：多个 volatile 变量在不同时刻被中断更新，主循环读取时无法保证它们属于同一个时间点，计算结果不一致
- **解决方案**：
```c
// 错误：直接读取多个 volatile 变量，可能在中断中途被更新
float freq = (float)(g_count2 - g_count1) / (float)(g_time2 - g_time1);

// 正确：用快照结构体，关中断原子读取
typedef struct {
    uint32_t count;
    uint32_t timestamp;
    uint8_t  valid;
} CaptureSnapshot_t;

volatile CaptureSnapshot_t g_snapshot;  // 在 ISR 中写入

// 主循环中读取快照
CaptureSnapshot_t snap;
__disable_irq();
snap = g_snapshot;      // 结构体整体复制，同一时间点
__enable_irq();

if (snap.valid) {
    float freq = 1000.0f / (float)snap.timestamp;
}
```
- **注意**：结构体拷贝不是原子操作，必须在关中断保护下执行；关中断区间尽量短
- **检索关键词**：volatile · 中断 · 快照 · 竞态 · 频率计算 · 数据不一致
- **来源项目**：upboard · 日期：—

---

## [I2C] 阻塞读取标准流程（HAL 轮询）

- **适用场景**：传感器读取，时序严格，对实时性要求不高
- **关键代码**：
```c
// 写寄存器地址，然后读数据（最常见的 I2C 传感器操作）
// 注意：HAL 使用 8 位地址格式，需左移 1 位
HAL_I2C_Mem_Read(&hi2c1,
    DEV_ADDR << 1,   // 7位地址左移
    REG_ADDR,        // 寄存器地址
    I2C_MEMADD_SIZE_8BIT,
    buf, len,
    HAL_MAX_DELAY);  // 生产代码替换为具体超时值，如 100

// HAL_MAX_DELAY 在生产代码中应替换为具体超时值
// 超时值单位是 ms，I2C 100kHz 下读 6 字节约需 1ms，留 10 倍余量
```
- **注意**：某些器件需要 Repeated Start，用 `HAL_I2C_Mem_Read` 而非手动 Transmit + Receive
- **检索关键词**：I2C · 传感器 · 读取 · HAL · 轮询
- **来源项目**：— · 日期：—

---

## 格式模板（新增时复制）

```
## [外设/类别] 模式名称

- **症状**（如果是坑）或 **适用场景**（如果是套路）：
- **根因**：
- **解决方案**：
- **注意**：
- **检索关键词**：
- **来源项目**：项目名 · 日期
```
