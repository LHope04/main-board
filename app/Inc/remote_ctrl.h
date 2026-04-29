#ifndef __REMOTE_CTRL_H
#define __REMOTE_CTRL_H

#include "stm32f4xx_hal.h"

/******************************************************************************
 * STUB: remote_ctrl — 有线遥控接口 (USART1, PA9 TX / PA10 RX, 115200 8N1, AF7)
 * ----------------------------------------------------------------------------
 * 留空原因   : 协议待定（未确认外接的有线遥控器型号 / 帧格式 / 业务命令集）
 * 后续接手   : 在 RemoteCtrl_Poll() 内补帧解析；RemoteCtrl_RxISR() 接状态机
 * 不实现的后果: USART1 字节被静默丢弃；不阻塞其他模块；不影响 IWDG / 主循环
 * 上下游    : 上游 = 外接有线遥控器；下游 = 期望喂 EspComm_GearCmd 等价的命令
 * 验收     : 接到一帧合法命令后,业务下游能看到对应的 GearCmd.updated=1
 ******************************************************************************/

void RemoteCtrl_Init(UART_HandleTypeDef *huart);
void RemoteCtrl_RxISR(void);   /* USART1_IRQHandler 调; 必须读 DR 清 RXNE */
void RemoteCtrl_Poll(void);    /* 主循环调; TODO(stub): 帧解析 */

#endif /* __REMOTE_CTRL_H */
