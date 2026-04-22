# RTOS Patterns
> FreeRTOS 任务、队列、信号量的通用使用模式。

---

## 使用说明

涉及 RTOS 开发时检索。包含正确用法和常见陷阱。

---

<!-- 示例格式：

## 任务间通信：队列传递传感器数据

**适用场景**：采集任务产生数据，处理任务消费数据，解耦两者

**关键代码**：
```c
// 定义
QueueHandle_t xSensorQueue;
xSensorQueue = xQueueCreate(10, sizeof(SensorData_t));

// 生产者（采集任务）
SensorData_t data = {temp, humi};
xQueueSend(xSensorQueue, &data, 0);  // 不阻塞，满则丢弃

// 消费者（处理任务）
SensorData_t rx;
if (xQueueReceive(xSensorQueue, &rx, pdMS_TO_TICKS(100)) == pdPASS) {
    // 处理数据
}
```

**注意事项**：
- 在 ISR 中使用 xQueueSendFromISR，不能用 xQueueSend
- 队列深度根据产生速度和消费速度的差值来定
- 传递结构体时注意内存对齐

**来源项目**：— 日期：—

-->
