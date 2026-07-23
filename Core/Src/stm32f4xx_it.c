/**
 * @file    Core/Src/stm32f4xx_it.c
 * @brief   Interrupt Service Routines (V6 board layout).
 */
#include "main.h"
#include "stm32f4xx_it.h"

/* 业务模块的 ISR 入口 (各模块 RxISR 直接在 USARTx_IRQHandler 里调) */
#include "esp_comm.h"
#include "led_rgb.h"
#include "power_ctrl.h"

/* 阶段 7/10 新增模块的 ISR 入口 — 弱符号 stub, 模块实现后替换 */
__attribute__((weak)) void SamplerComm_RxISR(void) {
    /* TODO(stub): 阶段 7 实现, 必须读 DR 清 RXNE 防中断风暴 */
    extern UART_HandleTypeDef huart3;
    if (huart3.Instance) (void)huart3.Instance->DR;
}
__attribute__((weak)) void RemoteCtrl_RxISR(void) {
    /* TODO(stub): 阶段 10 实现 */
    extern UART_HandleTypeDef huart1;
    if (huart1.Instance) (void)huart1.Instance->DR;
}
__attribute__((weak)) void IotCtrl_RxISR(void) {
    /* TODO(stub): 阶段 10 实现 */
    extern UART_HandleTypeDef huart2;
    if (huart2.Instance) (void)huart2.Instance->DR;
}

extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim7;

void NMI_Handler(void)        { while (1) {} }
void HardFault_Handler(void)  { while (1) {} }
void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* ===== Peripheral IRQs ===== */

void TIM3_IRQHandler(void)   { HAL_TIM_IRQHandler(&htim3); }   /* FAN_FB IC */
void TIM4_IRQHandler(void)   { HAL_TIM_IRQHandler(&htim4); }   /* MCF8329A FG IC */
void TIM7_IRQHandler(void)                                    /* LED 呼吸灯软 PWM @2kHz */
{
    if (__HAL_TIM_GET_FLAG(&htim7, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_IT(&htim7, TIM_IT_UPDATE);
        LedRgb_PwmTick();
        PowerCtrl_PumpPwmTick2kHz();
    }
}

void USART1_IRQHandler(void) { RemoteCtrl_RxISR(); }   /* 有线遥控 stub */
void USART2_IRQHandler(void) { IotCtrl_RxISR();    }   /* BC260Y stub */
void USART3_IRQHandler(void) { SamplerComm_RxISR(); }  /* 采样板 */
void USART6_IRQHandler(void) { EspComm_RxISR();    }   /* ESP32-C3 BLE */
