# Known Bugs
> 芯片 / HAL / 工具链已知问题。开发前检索，避免重复踩坑。
> 格式：症状 → 根因 → 解决方案。只追加，不修改历史。

---

## [STM32] TIM8 双中断向量

- **症状**：TIM8 的捕获或溢出中断永远不触发，`HAL_TIM_IC_CaptureCallback` / 溢出回调从未执行
- **根因**：TIM8 是高级定时器，中断向量拆成两个：溢出（UP）和捕获比较（CC）分别对应不同的 IRQ。通用定时器（TIM2-5）只有一个 `TIMx_IRQn`，容易被误套到 TIM8
- **解决方案**：
```c
// TIM8 必须分别使能两个中断向量
HAL_NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 0, 0);   // 溢出中断
HAL_NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);
HAL_NVIC_SetPriority(TIM8_CC_IRQn, 0, 0);          // 捕获比较中断
HAL_NVIC_EnableIRQ(TIM8_CC_IRQn);

// 两个 Handler 都要写
void TIM8_UP_TIM13_IRQHandler(void) { HAL_TIM_IRQHandler(&htim8); }
void TIM8_CC_IRQHandler(void)       { HAL_TIM_IRQHandler(&htim8); }
```
- **检索关键词**：TIM8 · 捕获 · 中断不触发 · IC · 溢出
- **涉及芯片**：STM32F4xx / STM32F7xx（凡带 TIM8 的型号）
- **来源项目**：upboard · 日期：—

---

## [ST-LINK] `-r32 count > 2` 返回数据不完整

- **症状**：`ST-LINK_CLI.exe -r32 <addr> 4` 返回字数少于请求数，多变量联合诊断结果缺失
- **根因**：部分 ST-LINK 固件版本在 count > 2 时行为不可靠
- **解决方案**：
```bash
# 错误：依赖 count > 2
ST-LINK_CLI.exe -c SWD HotPlug -r32 0x20000100 4

# 正确：每个地址单独读
ST-LINK_CLI.exe -c SWD HotPlug -r32 0x20000100 1
ST-LINK_CLI.exe -c SWD HotPlug -r32 0x20000104 1
ST-LINK_CLI.exe -c SWD HotPlug -r32 0x20000108 1
```
- **检索关键词**：ST-LINK · r32 · 内存读取 · 不完整
- **来源项目**：upboard · 日期：—

---

## 格式模板（新增时复制）

```
## [芯片/工具] 问题标题

- **症状**：
- **根因**：
- **解决方案**：
- **检索关键词**：
- **涉及芯片**：
- **参考**：（数据手册章节 / Errata 编号）
- **来源项目**：项目名 · 日期
```
