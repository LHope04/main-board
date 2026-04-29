#ifndef __IOT_CTRL_H
#define __IOT_CTRL_H

#include "stm32f4xx_hal.h"

/******************************************************************************
 * STUB: iot_ctrl — 物联网模块 BC260Y 接口
 * (USART2, PA2 TX / PA3 RX, 115200 8N1, AF7, 经 R15/R18 100Ω 串联)
 * ----------------------------------------------------------------------------
 * 留空原因   : BC260Y AT 命令交互 / 业务协议待定;模块本身可能尚未到位
 * 后续接手   : IotCtrl_Poll() 实现 AT 命令收发框架 (URC 处理 + 命令队列)
 * 不实现的后果: USART2 字节被静默丢弃；不阻塞其他模块
 * 上下游    : 上游 = BC260Y NB-IoT/Cat-M 模组; 下游 = 期望对接业务上报通路
 * 验收     : AT 握手成功 / 网络注册成功 / 业务消息能上下行
 ******************************************************************************/

void IotCtrl_Init(UART_HandleTypeDef *huart);
void IotCtrl_RxISR(void);
void IotCtrl_Poll(void);

#endif /* __IOT_CTRL_H */
