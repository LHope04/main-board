/**
 * @file    Core/Src/main.c
 * @brief   Main program for V6 board (STM32F407VET6).
 *
 * Pin allocation reference: 板子/SCH_Schematic6_*.pdf "IO 未分配" page.
 * Migration plan: /Users/mac/.claude/plans/joyful-tinkering-lemur.md (阶段 1).
 *
 * Stage 1 scope: wiring init only. Business modules (fan/compressor/sensor_acq)
 * keep their old API surface but call sites in main loop are pruned to whatever
 * compiles. Real port happens in stages 2~10.
 */
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include "stm32f4xx_hal_tim.h"

#include "ina226.h"
#include "power_ctrl.h"
#include "fan_ctrl.h"
#include "compressor_ctrl.h"
#include "sensor_acq.h"
#include "buzzer.h"
#include "esp_comm.h"
#include "ota.h"
#include "led_rgb.h"
#include "button.h"
#include "sampler_comm.h"
#include "remote_ctrl.h"
#include "iot_ctrl.h"
#include "mcf8329a.h"

/* ===== Peripheral handles ===== */
TIM_HandleTypeDef htim1;   /* BEEP_CTRL    PB14 TIM1_CH2N AF1 (高级定时器, 168MHz, MOE 必启) */
TIM_HandleTypeDef htim2;   /* FAN_PWM_CTRL PA15 TIM2_CH1  AF1 (84MHz, 20kHz PWM) */
TIM_HandleTypeDef htim3;   /* FAN_FB_OUT   PB4  TIM3_CH1  AF2 (84MHz, IC 1MHz tick) */
TIM_HandleTypeDef htim4;   /* MCF8329A FG  PB9  TIM4_CH4  AF2 (84MHz, IC 1MHz tick) */

/* === I2C bus scan results (temporary, stage 5 verification) === */
volatile uint8_t g_i2c1_devs[16];
volatile uint8_t g_i2c2_devs[16];
volatile uint8_t g_i2c3_devs[16];
volatile uint8_t g_i2c1_count = 0;
volatile uint8_t g_i2c2_count = 0;
volatile uint8_t g_i2c3_count = 0;

/* I2C3 ping 0x01 (MCF8329A 默认) 诊断 */
volatile uint32_t g_i2c3_ping_rc  = 0xFF;   /* HAL_OK=0, HAL_ERROR=1, HAL_BUSY=2, HAL_TIMEOUT=3 */
volatile uint32_t g_i2c3_ping_sr1 = 0;
volatile uint32_t g_i2c3_ping_sr2 = 0;
volatile uint32_t g_i2c3_err_code = 0;
volatile uint32_t g_i2c3_state    = 0;       /* HAL_I2C_StateTypeDef: READY=0x20 */

/* MCF8329A spin diagnostics */
static MCF8329A_Device s_mcf;
volatile uint32_t g_mcf_spin_rc      = 0xFFU;   /* HAL_OK=0 等 */
volatile uint32_t g_mcf_status_rc    = 0xFFU;
volatile uint32_t g_mcf_algo_status  = 0;       /* ALGO_STATUS 0xE4 */
volatile uint32_t g_mcf_gate_fault   = 0;       /* GATE_DRIVER_FAULT_STATUS 0xE0 */
volatile uint32_t g_mcf_ctrl_fault   = 0;       /* CONTROLLER_FAULT_STATUS 0xE2 */
volatile uint32_t g_mcf_algo_state   = 0xFFFFU; /* ALGORITHM_STATE 0x196 (0=IDLE, 7=OPEN_LOOP, Eh=FAULT) */
volatile uint32_t g_mcf_state_rc     = 0xFFU;
volatile uint32_t g_mcf_kick_rc      = 0xFFU;
volatile uint32_t g_mcf_clrflt_rc    = 0xFFU;
volatile uint32_t g_mcf_cfg_rc       = 0xFFU;
volatile uint32_t g_mcf_vm_voltage   = 0;       /* VM_VOLTAGE @ 0x45C, 反映 motor 母线 24V 是否到位 */
volatile uint32_t g_mcf_phase_a      = 0;       /* PHASE_VOLTAGE_VA @ 0x460 */
volatile uint32_t g_mcf_motor_su1_rb = 0;       /* 读回 MOTOR_STARTUP1 (0x84) 验证 shadow 写持久 */
volatile uint32_t g_mcf_cl1_rb       = 0;       /* CLOSED_LOOP1 (0x88) — 含 MAX_SPEED, 限速根因 */
volatile uint32_t g_mcf_cl4_rb       = 0;       /* CLOSED_LOOP4 (0x8E) — 速度环 / IL 限制 */
volatile uint32_t g_mcf_dev_cfg2_rb  = 0;       /* DEVICE_CONFIG2 (0xA8) — I2C addr 来源 */
/* SWD-controlled override for shadow registers. Non-zero = motor stops, write,
 * read-back, restart at spin_duty. Set to 0 after handled.
 * 用于实验性突破 EEPROM 限速 — e.g. CLOSED_LOOP1 bump MAX_SPEED. */
volatile uint32_t g_mcf_cl1_set      = 0;       /* 写入 CLOSED_LOOP1 (0x88) 的新值, 0=不动 */
volatile uint32_t g_mcf_cl1_set_rc   = 0xFFU;   /* 写结果 */
volatile uint32_t g_mcf_cl1_set_done = 0;       /* 已处理次数 */
/* MOTOR_STARTUP1/2 (0x84/0x86) — 重负载启动调参用.
 * STARTUP1: 30:29 MTR_STARTUP, 28:25 ALIGN_RAMP_RATE, 24:21 ALIGN_TIME, 20:17 ALIGN_OR_SLOW_CURRENT.
 * STARTUP2: 30:27 OL_ILIMIT, 26:23 OL_ACC_A1, 22:19 OL_ACC_A2, 17:13 OPN_CL_HANDOFF_THR. */
volatile uint32_t g_mcf_motor_su2_rb       = 0;
volatile uint32_t g_mcf_motor_su2_set      = 0;       /* SWD 写入新值; 0=不动 */
volatile uint32_t g_mcf_motor_su2_set_rc   = 0xFFU;
volatile uint32_t g_mcf_motor_su2_set_done = 0;
volatile uint32_t g_mcf_motor_su1_set      = 0;
volatile uint32_t g_mcf_motor_su1_set_rc   = 0xFFU;
volatile uint32_t g_mcf_motor_su1_set_done = 0;
/* FAULT_CONFIG1 (0x90) — 关 LOCK_ILIMIT_MODE / MTR_LCK_MODE 让重负载下电流持续灌入. */
volatile uint32_t g_mcf_fault_cfg1_rb       = 0;
volatile uint32_t g_mcf_fault_cfg1_set      = 0;
volatile uint32_t g_mcf_fault_cfg1_set_rc   = 0xFFU;
volatile uint32_t g_mcf_fault_cfg1_set_done = 0;
/* PIN_CONFIG (0xA4) — bits[1:0] SPEED_MODE: 0=Analog,1=PWM Duty,2=Register Override (I2C only),3=PWM Freq. */
volatile uint32_t g_mcf_pin_cfg_rb          = 0;
volatile uint32_t g_mcf_pin_cfg_set         = 0;
volatile uint32_t g_mcf_pin_cfg_set_rc      = 0xFFU;
volatile uint32_t g_mcf_pin_cfg_set_done    = 0;
/* SWD 写 g_mcf_ee_commit_req=0x8A500000 触发一次 EEPROM_WRT(ALGO_CTRL1).
 * 把当前 shadow 区(0x80-0xAE)永久烧进 EEPROM,断电不丢. */
volatile uint32_t g_mcf_ee_commit_req       = 0;
volatile uint32_t g_mcf_ee_commit_rc        = 0xFFU;
volatile uint32_t g_mcf_ee_commit_done      = 0;
/* Boot-time CLOSED_LOOP4 override: motor IDLE 时写 shadow, 不动 EEPROM.
 * Motor Studio 确认 MAX_SPEED = CL4 低 12 位 (CL4=0x08D904B0, MAX=0x4B0=1200).
 * 实测电机 ~130Hz electrical, 编码约 6 units/Hz.
 * 0x708 (=1800) → 300Hz electrical = 4500 RPM @ 8极 (规格上限).
 * 新 CL4 = (0x08D904B0 & ~0xFFF) | 0x708 = 0x08D90708.
 * 设 0 跳过, 用 EEPROM 默认 (200Hz / 3000 RPM). */
volatile uint32_t g_mcf_cl4_boot_target = 0U;           /* 默认 0 = 不覆盖 EEPROM CL4; Motor Studio 已写好 EEPROM */
volatile uint32_t g_mcf_cl4_boot_rc     = 0xFFU;
/* DIR 引脚 PD3: 0 = CW (默认). 实测 EEPROM PIN_CONFIG 把 DIR 引脚屏蔽了,
 * 拉 PD3 高低不影响电机方向. 当前电机机械方向恰好是芯片 BEMF 估算的 "反向",
 * 所以 speed_fdbk 一直读出负值, 但这只是符号约定, 电机功能正常.
 * 上报转速时取 abs(speed_fdbk) 即可. */
volatile uint8_t  g_mcf_dir_cw          = 0U;
volatile uint32_t g_mcf_mpet_status  = 0;       /* ALGO_STATUS_MPET @ 0xE8 */
volatile uint32_t g_mcf_mpet_rc      = 0xFFU;
volatile uint16_t g_mcf_spin_duty    = 0x7FFF;  /* 100% duty — 出厂模式直接拉满 */
volatile uint32_t g_mcf_fg_speed     = 0;       /* FG_SPEED_FDBK @ 0x19C */
volatile uint32_t g_mcf_speed_fdbk   = 0;       /* SPEED_FDBK @ 0x76E (closed-loop 估速) */
/* 当 = 1, 主循环跳过所有 I2C3 → MCF8329A 操作 (写 / 读 / WD tickle).
 * 让 Motor Studio 独占总线调参. SWD: mww 0x<g_mcf_i2c_disable_addr> 1 关闭. */
volatile uint8_t  g_mcf_i2c_disable  = 0;


/*
 * INA226 sensor instances (V6 layout):
 *   sensors[0] — I2C1, 24V_BAT_UIP_INA226 (锂电池功率, A0/A1=GND/GND)
 *   sensors[1] — I2C1, 24V_YSJ_SENSOR_INA226 (压缩机功率, 不同 A0/A1 区分)
 *   sensors[2] — I2C2, 12V_VCC_UIP_INA226 (电瓶输入功率)
 * 实际地址 / cal_val / Rshunt 在阶段 5 按硬件标定确认。
 */
INA226_Device sensors[3];

void SystemClock_Config(void);

/* ===== TIM1: BEEP_CTRL PB14 / TIM1_CH2N AF1 ===== */
/* APB2=168MHz, TIM1 走 168MHz, PSC=167 → 1MHz tick. ARR 由 Buzzer_PlayTone 动态设置.
 * 高级定时器 — Buzzer_Init 必须 __HAL_TIM_MOE_ENABLE(), 否则 CH2N 无信号. */
void MX_TIM1_BEEP_Init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 167;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 999;       /* 默认 1kHz, ARR 由蜂鸣器动态改 */
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    oc.Pulse        = 499;                    /* 默认 50% duty */
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2);
}

/* ===== TIM2: FAN_PWM_CTRL PA15 / TIM2_CH1 AF1 ===== */
/* APB1=84MHz × 2 = 84MHz (TIMxCLK), PSC=83 → 1MHz tick, ARR=49 → 20kHz PWM.
 * Duty = CCR / (ARR+1) = CCR / 50 → percent → CCR = percent * 50 / 100. */
void MX_TIM2_FANPWM_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 83;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 49;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim2);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.Pulse      = 0;
    HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1);
}

/* ===== TIM3: FAN_FB_OUT PB4 / TIM3_CH1 AF2 (输入捕获) ===== */
/* APB1×2=84MHz, PSC=83 → 1MHz tick, ARR=65535 → 65.5ms 满量程. */
void MX_TIM3_FANIC_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 83;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 65535;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&htim3);

    TIM_IC_InitTypeDef ic = {0};
    ic.ICPolarity  = TIM_ICPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter    = 0x0F;
    HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_1);

    HAL_NVIC_SetPriority(TIM3_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

/* ===== TIM4: MCF8329A FG PB9 / TIM4_CH4 AF2 (输入捕获) ===== */
/* APB1×2=84MHz, PSC=83 → 1MHz tick, ARR=65535 → 65.5ms 满量程. */
void MX_TIM4_CMPRFG_Init(void)
{
    __HAL_RCC_TIM4_CLK_ENABLE();

    htim4.Instance               = TIM4;
    htim4.Init.Prescaler         = 83;
    htim4.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim4.Init.Period            = 65535;
    htim4.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&htim4);

    TIM_IC_InitTypeDef ic = {0};
    ic.ICPolarity  = TIM_ICPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter    = 0x0F;
    HAL_TIM_IC_ConfigChannel(&htim4, &ic, TIM_CHANNEL_4);

    HAL_NVIC_SetPriority(TIM4_IRQn, 3, 1);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* BKP2R = 0xD15AB1ED 表示 Motor Studio 调试模式持久化:
     * 上电后 STM32 不再 init I2C3/不访问 MCF8329A, 让 Motor Studio 独占总线.
     * 清回 STM32 接管: SWD 写 g_mcf_i2c_disable = 0 (运行时 handler 会清 BKP2R). */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    g_mcf_i2c_disable = (RTC->BKP2R == 0xD15AB1EDU) ? 1U : 0U;

    MX_GPIO_Init();
    MX_TIM1_BEEP_Init();
    MX_TIM2_FANPWM_Init();
    MX_TIM3_FANIC_Init();
    MX_TIM4_CMPRFG_Init();
    MX_I2C1_Init();
    MX_I2C2_Init();
    MX_I2C3_Init();   /* MCF8329A 通信 */
    MX_USART1_UART_Init();   /* RemoteCtrl  stub */
    MX_USART2_UART_Init();   /* IotCtrl     stub */
    MX_USART3_UART_Init();   /* SamplerComm 阶段 7 */
    MX_USART6_UART_Init();   /* EspComm     阶段 8 */

    /* INA226 (阶段 5 标定地址 + cal_val + LSB) */
    INA226_Init(&sensors[0], &hi2c1, 0x80, 0x0355, 0.0012f);  /* 24V_BAT  A1=GND A0=GND */
    INA226_Init(&sensors[1], &hi2c1, 0x8A, 0x0355, 0.0012f);  /* 24V_YSJ  A1=VS  A0=VS  (实测) */
    INA226_Init(&sensors[2], &hi2c2, 0x80, 0x0355, 0.0012f);  /* 12V_VCC  待确认 */

    /* 业务模块 Init: 接口签名保留, 内部实现各阶段重写 */
    SensorAcq_Init();                       /* 阶段 7 改 USART3 收帧 */
    FanCtrl_Init(&htim2, &htim3);           /* 阶段 4: PWM=TIM2_CH1, IC=TIM3_CH1 */
    /* CompressorCtrl_Init() — 暂停: 用户外接调试器直连 MCF8329A,
     * 主控不动 PD3/PD4/PB9 等控制信号. */
    Buzzer_Init(&htim1);                    /* 阶段 2: TIM1_CH2N + MOE */
    EspComm_Init(&huart6);                  /* 阶段 8: USART6 (旧 huart2) */
    LedRgb_Init();                          /* 阶段 2: PE2/PE3/PE4 */
    Button_Init();                          /* 阶段 2: PC13 短/长按 */
    SamplerComm_Init(&huart3);              /* 阶段 7: USART3 采样板 */
    RemoteCtrl_Init(&huart1);               /* 阶段 10: stub */
    IotCtrl_Init(&huart2);                  /* 阶段 10: stub */

    PowerCtrl_StartupSequence();            /* 阶段 3: 新引脚时序 */

    /* 清 I2C1/I2C2/I2C3 总线 BUSY 锁. */
    {
        I2C_HandleTypeDef *handles[3] = { &hi2c1, &hi2c2, &hi2c3 };
        for (int i = 0; i < 3; i++) {
            handles[i]->Instance->CR1 |=  I2C_CR1_SWRST;
            HAL_Delay(1);
            handles[i]->Instance->CR1 &= ~I2C_CR1_SWRST;
            HAL_I2C_DeInit(handles[i]);
            HAL_I2C_Init(handles[i]);
        }
    }

    /* MCF8329A wake cycle: 强制 SPEED/WAKE LOW → 100ms → HIGH 触发 wake edge.
     * 防止芯片上电时已经 HIGH 但错过 wake event 卡 sleep. */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);   /* WAKE LOW */
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);     /* WAKE HIGH */
    HAL_Delay(100);                                          /* wake-up time */

    /* === MCF8329A 直接 I2C 启动: 写 ALGO_DEBUG1 (0xEC) 用 SPEED_OVER_RIDE 强转 ===
     * I2C mode 必须周期 tickle WATCHDOG_TICKLE (ALGO_CTRL1 bit10), 否则芯片
     * 进 ext-WD fault 不响应. 启动序列:
     *   1. 清故障 + tickle WD
     *   2. 等 50ms 让芯片应用
     *   3. 读 status / algo state, 写 spin duty (覆盖默认 SPEED_MODE 用 DIGITAL_SPEED_CTRL) */
    if (!g_mcf_i2c_disable) {
        MCF8329A_Init(&s_mcf, &hi2c3, MCF8329A_HAL_ADDR_DEFAULT);
        MCF8329A_SetDir(g_mcf_dir_cw);  /* 应用方向, Init 内部默认 CW=0 */
        HAL_Delay(20);
        g_mcf_clrflt_rc = (uint32_t)MCF8329A_ClearFault(&s_mcf);
        HAL_Delay(20);
        g_mcf_kick_rc   = (uint32_t)MCF8329A_KickWatchdog(&s_mcf);
        HAL_Delay(20);

        /* 等同 Motor Studio "I2C Speed Command Percentage" 滑块模式:
         * 不覆盖 shadow 寄存器, 只用 EEPROM 里的出厂/已固化配置 + SPEED_OVERRIDE。 */
        g_mcf_cfg_rc = HAL_OK;
        (void)MCF8329A_Read32(&s_mcf, 0x000084U, (uint32_t *)&g_mcf_motor_su1_rb);
        (void)MCF8329A_Read32(&s_mcf, 0x000086U, (uint32_t *)&g_mcf_motor_su2_rb);
        (void)MCF8329A_Read32(&s_mcf, 0x000090U, (uint32_t *)&g_mcf_fault_cfg1_rb);
        (void)MCF8329A_Read32(&s_mcf, 0x000088U, (uint32_t *)&g_mcf_cl1_rb);
        (void)MCF8329A_Read32(&s_mcf, 0x00008EU, (uint32_t *)&g_mcf_cl4_rb);
        (void)MCF8329A_Read32(&s_mcf, 0x0000A8U, (uint32_t *)&g_mcf_dev_cfg2_rb);
        (void)MCF8329A_Read32(&s_mcf, 0x0000A4U, (uint32_t *)&g_mcf_pin_cfg_rb);

        if (g_mcf_cl4_boot_target != 0U) {
            g_mcf_cl4_boot_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x00008EU,
                                                           g_mcf_cl4_boot_target);
            HAL_Delay(20);
            (void)MCF8329A_Read32(&s_mcf, 0x00008EU, (uint32_t *)&g_mcf_cl4_rb);
            HAL_Delay(500);
            IWDG->KR = 0xAAAAU;
            (void)MCF8329A_ClearFault(&s_mcf);
        }

        g_mcf_mpet_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x0000EEU, 0U);
        HAL_Delay(20);
        g_mcf_status_rc = (uint32_t)MCF8329A_RefreshStatus(&s_mcf);
        g_mcf_algo_status = s_mcf.last_algo_status;
        g_mcf_gate_fault  = s_mcf.last_gate_fault;
        g_mcf_ctrl_fault  = s_mcf.last_ctrl_fault;
        g_mcf_state_rc    = (uint32_t)MCF8329A_ReadAlgoState(&s_mcf, (uint32_t *)&g_mcf_algo_state);
        g_mcf_spin_rc = (uint32_t)MCF8329A_SpinDuty(&s_mcf, g_mcf_spin_duty);
    } else {
        /* Motor Studio 调试模式: 立刻释放 I2C3 总线给外部主控.
         * WAKE/DRVOFF/Brake 还是 STM32 GPIO 输出 (上面已置 enable 状态), 不动. */
        HAL_I2C_DeInit(&hi2c3);
        GPIO_InitTypeDef gi = {0};
        gi.Mode = GPIO_MODE_INPUT;
        gi.Pull = GPIO_NOPULL;
        gi.Pin  = GPIO_PIN_8;  HAL_GPIO_Init(GPIOA, &gi);  /* SCL */
        gi.Pin  = GPIO_PIN_9;  HAL_GPIO_Init(GPIOC, &gi);  /* SDA */
    }

    Buzzer_PlayHajimi();                    /* 开机大疆音; 内部喂狗 */

    while (1) {
        /* IWDG 喂狗 — Bootloader 启了, App 必须续 */
        IWDG->KR = 0xAAAAU;

        uint32_t now_ms = HAL_GetTick();

        /* === 高频 (~10ms 节拍) === */
        LedRgb_Tick(now_ms);
        switch (Button_Poll()) {
            case BUTTON_EVT_SHORT:
                LedRgb_NextSolidColor();
                Buzzer_PlayTone(2000, 50);
                break;
            case BUTTON_EVT_LONG:
                LedRgb_SetMode(LED_MODE_OFF);
                Buzzer_PlayTone(800, 500);
                break;
            default: break;
        }
        /* === I2C3 持续扫描 (debug, 500ms 一次, watch g_i2c3_count/devs/ping_*) === */
        {
            static uint32_t s_last_i2c3_scan = 0;
            if ((uint32_t)(now_ms - s_last_i2c3_scan) >= 500U) {
                s_last_i2c3_scan = now_ms;
                g_i2c3_count = 0;
                for (uint8_t a = 1; a < 128; a++) {
                    HAL_StatusTypeDef rc = HAL_I2C_IsDeviceReady(&hi2c3, (uint16_t)(a << 1), 1, 2);
                    if (rc == HAL_OK) {
                        if (g_i2c3_count < 16) g_i2c3_devs[g_i2c3_count++] = a;
                    }
                    if (a == 0x01) {
                        g_i2c3_ping_rc  = (uint32_t)rc;
                        g_i2c3_ping_sr1 = hi2c3.Instance->SR1;
                        g_i2c3_ping_sr2 = hi2c3.Instance->SR2;
                        g_i2c3_err_code = hi2c3.ErrorCode;
                        g_i2c3_state    = (uint32_t)hi2c3.State;
                    }
                }
            }
        }

        EspComm_Poll();
        SamplerComm_Poll();
        SensorAcq_Tick(now_ms);
        RemoteCtrl_Poll();
        IotCtrl_Poll();

        if (EspComm_TakeOtaSelfTestRequest()) {
            Ota_SelfTest(32U * 1024U);
        }
        if (OtaProto_TakeResetRequest()) {
            HAL_Delay(50);
            __DSB();
            NVIC_SystemReset();
        }

        /* === 100ms 分流: INA226 / Gear cmd === */
        {
            static uint32_t s_last_100 = 0;
            if ((uint32_t)(now_ms - s_last_100) >= 100U) {
                s_last_100 = now_ms;

                for (int i = 0; i < 3; i++) {
                    INA226_UpdateData(&sensors[i]);
                }

                EspComm_GearCmd *cmd = EspComm_GetGearCmd();
                if (cmd->updated) {
                    cmd->updated = 0;
                    if (cmd->on) {
                        FanCtrl_Enable(1);
                        FanCtrl_SetDuty(100);
                        PowerCtrl_EnablePump(1);

                        /* gear 1~10 → duty 75~100% (gear 10 = MAX 100%).
                         * 压缩机静态起转实测 ≥75%, 低于此值会"嗡嗡转不起来". */
                        int16_t gear = cmd->gear;
                        if (gear < 1)  gear = 1;
                        if (gear > 10) gear = 10;
                        uint8_t duty = (uint8_t)(75 + (gear - 1) * 25 / 9);
                        if (duty > 100) duty = 100;
                        CompressorCtrl_SetBrake(0);     /* 释放 brake, 电机可转 */
                        CompressorCtrl_SetDuty(duty);   /* 写 g_mcf_spin_duty, 下 200ms tick 生效 */
                    } else {
                        FanCtrl_Enable(0);
                        FanCtrl_SetDuty(0);
                        PowerCtrl_EnablePump(0);

                        CompressorCtrl_SetDuty(0);      /* spin_duty=0 → MCF8329A 进 IDLE */
                        CompressorCtrl_SetBrake(1);     /* 主动 brake 快速停转 */
                    }
                }
            }
        }

        /* === 500ms 分流: 采样板心跳, OTA mark boot === */
        {
            static uint32_t s_last_500 = 0;
            if ((uint32_t)(now_ms - s_last_500) >= 500U) {
                s_last_500 = now_ms;

                static uint8_t  s_hb_seq = 0;
                SamplerComm_SendHeartbeat(s_hb_seq++);

                static uint8_t s_boot_marked = 0;
                if (!s_boot_marked && now_ms >= 10000U) {
                    s_boot_marked = 1;
                    (void)Ota_MarkBootOk();
                }
            }
        }

        /* I2C3 释放 / 接管 — 监测 g_mcf_i2c_disable 跳变, 把 PA8 SCL / PC9 SDA
         * 切到 input no-pull (tri-state), 让 Motor Studio 独占 I2C 总线. */
        {
            static uint8_t s_last_disable = 0xFF;
            if (g_mcf_i2c_disable != s_last_disable) {
                s_last_disable = g_mcf_i2c_disable;
                /* Persist to RTC backup, 跨重启生效 */
                HAL_PWR_EnableBkUpAccess();
                RTC->BKP2R = g_mcf_i2c_disable ? 0xD15AB1EDU : 0U;
                if (g_mcf_i2c_disable) {
                    /* 释放: I2C3 peripheral 关闭 + SCL/SDA 设成浮空输入 */
                    HAL_I2C_DeInit(&hi2c3);
                    GPIO_InitTypeDef gi = {0};
                    gi.Mode = GPIO_MODE_INPUT;
                    gi.Pull = GPIO_NOPULL;
                    gi.Pin  = GPIO_PIN_8;
                    HAL_GPIO_Init(GPIOA, &gi);
                    gi.Pin  = GPIO_PIN_9;
                    HAL_GPIO_Init(GPIOC, &gi);
                } else {
                    /* 接管: 重新初始化 I2C3 + SWRST 解 BUSY 锁 */
                    MX_I2C3_Init();
                    hi2c3.Instance->CR1 |=  I2C_CR1_SWRST;
                    HAL_Delay(1);
                    hi2c3.Instance->CR1 &= ~I2C_CR1_SWRST;
                    HAL_I2C_DeInit(&hi2c3);
                    HAL_I2C_Init(&hi2c3);
                }
            }
        }

        /* === SWD-triggered shadow register override (CLOSED_LOOP1 MAX_SPEED) ===
         * 流程: spin=0 → 等 200ms 让芯片进 IDLE → 写 CL1 → 读回 → 恢复 spin_duty.
         * 一次性, 处理完清 g_mcf_cl1_set, 防止反复写。 */
        if (!g_mcf_i2c_disable && g_mcf_cl1_set != 0U) {
            uint16_t saved_duty = g_mcf_spin_duty;
            g_mcf_spin_duty = 0U;
            (void)MCF8329A_SpinDuty(&s_mcf, 0U);
            HAL_Delay(300);                     /* 让闭环停 + 喂狗时间 */
            IWDG->KR = 0xAAAAU;
            g_mcf_cl1_set_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x000088U, g_mcf_cl1_set);
            HAL_Delay(20);
            (void)MCF8329A_Read32(&s_mcf, 0x000088U, (uint32_t *)&g_mcf_cl1_rb);
            g_mcf_cl1_set = 0U;
            g_mcf_cl1_set_done++;
            g_mcf_spin_duty = saved_duty;
            (void)MCF8329A_SpinDuty(&s_mcf, saved_duty);
            IWDG->KR = 0xAAAAU;
        }

        /* === SWD-triggered shadow override (MOTOR_STARTUP2 0x86) ===
         * 重负载启动调参用. 改 OL_ILIMIT 增大开环驱动电流. shadow-only, 不动 EEPROM. */
        if (!g_mcf_i2c_disable && g_mcf_motor_su2_set != 0U) {
            uint16_t saved_duty = g_mcf_spin_duty;
            g_mcf_spin_duty = 0U;
            (void)MCF8329A_SpinDuty(&s_mcf, 0U);
            HAL_Delay(300);
            IWDG->KR = 0xAAAAU;
            g_mcf_motor_su2_set_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x000086U, g_mcf_motor_su2_set);
            HAL_Delay(20);
            (void)MCF8329A_Read32(&s_mcf, 0x000086U, (uint32_t *)&g_mcf_motor_su2_rb);
            g_mcf_motor_su2_set = 0U;
            g_mcf_motor_su2_set_done++;
            g_mcf_spin_duty = saved_duty;
            (void)MCF8329A_SpinDuty(&s_mcf, saved_duty);
            IWDG->KR = 0xAAAAU;
        }

        /* === SWD-triggered shadow override (MOTOR_STARTUP1 0x84) === */
        if (!g_mcf_i2c_disable && g_mcf_motor_su1_set != 0U) {
            uint16_t saved_duty = g_mcf_spin_duty;
            g_mcf_spin_duty = 0U;
            (void)MCF8329A_SpinDuty(&s_mcf, 0U);
            HAL_Delay(300);
            IWDG->KR = 0xAAAAU;
            g_mcf_motor_su1_set_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x000084U, g_mcf_motor_su1_set);
            HAL_Delay(20);
            (void)MCF8329A_Read32(&s_mcf, 0x000084U, (uint32_t *)&g_mcf_motor_su1_rb);
            g_mcf_motor_su1_set = 0U;
            g_mcf_motor_su1_set_done++;
            g_mcf_spin_duty = saved_duty;
            (void)MCF8329A_SpinDuty(&s_mcf, saved_duty);
            IWDG->KR = 0xAAAAU;
        }

        /* === SWD-triggered shadow override (FAULT_CONFIG1 0x90) === */
        if (!g_mcf_i2c_disable && g_mcf_fault_cfg1_set != 0U) {
            uint16_t saved_duty = g_mcf_spin_duty;
            g_mcf_spin_duty = 0U;
            (void)MCF8329A_SpinDuty(&s_mcf, 0U);
            HAL_Delay(300);
            IWDG->KR = 0xAAAAU;
            g_mcf_fault_cfg1_set_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x000090U, g_mcf_fault_cfg1_set);
            HAL_Delay(20);
            (void)MCF8329A_Read32(&s_mcf, 0x000090U, (uint32_t *)&g_mcf_fault_cfg1_rb);
            g_mcf_fault_cfg1_set = 0U;
            g_mcf_fault_cfg1_set_done++;
            g_mcf_spin_duty = saved_duty;
            (void)MCF8329A_SpinDuty(&s_mcf, saved_duty);
            IWDG->KR = 0xAAAAU;
        }

        /* === SWD-triggered shadow override (PIN_CONFIG 0xA4) ===
         * bits[1:0] SPEED_MODE: 0=Analog, 1=PWM Duty, 2=Register Override (I2C only), 3=PWM Freq. */
        if (!g_mcf_i2c_disable && g_mcf_pin_cfg_set != 0U) {
            uint16_t saved_duty = g_mcf_spin_duty;
            g_mcf_spin_duty = 0U;
            (void)MCF8329A_SpinDuty(&s_mcf, 0U);
            HAL_Delay(300);
            IWDG->KR = 0xAAAAU;
            g_mcf_pin_cfg_set_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x0000A4U, g_mcf_pin_cfg_set);
            HAL_Delay(20);
            (void)MCF8329A_Read32(&s_mcf, 0x0000A4U, (uint32_t *)&g_mcf_pin_cfg_rb);
            g_mcf_pin_cfg_set = 0U;
            g_mcf_pin_cfg_set_done++;
            g_mcf_spin_duty = saved_duty;
            (void)MCF8329A_SpinDuty(&s_mcf, saved_duty);
            IWDG->KR = 0xAAAAU;
        }

        /* === SWD-triggered EEPROM commit ===
         * 写 g_mcf_ee_commit_req = 0x8A500000 → 写一次 ALGO_CTRL1 触发 EEPROM_WRT.
         * 必须 motor IDLE 时执行,等待 300ms 让 chip 完成 EEPROM 写入. */
        if (!g_mcf_i2c_disable && g_mcf_ee_commit_req == 0x8A500000U) {
            uint16_t saved_duty = g_mcf_spin_duty;
            g_mcf_spin_duty = 0U;
            (void)MCF8329A_SpinDuty(&s_mcf, 0U);
            HAL_Delay(300);
            IWDG->KR = 0xAAAAU;
            g_mcf_ee_commit_rc = (uint32_t)MCF8329A_Write32(&s_mcf, 0x0000EAU, 0x8A500000U);
            HAL_Delay(150);
            IWDG->KR = 0xAAAAU;
            HAL_Delay(150);
            g_mcf_ee_commit_req = 0U;
            g_mcf_ee_commit_done++;
            /* 不自动恢复 spin_duty: 烧完用户手动确认状态再重启 */
            (void)saved_duty;
            IWDG->KR = 0xAAAAU;
        }

        /* === 200ms 分流: MCF8329A WD tickle + spin 重写 + 读故障 / 状态 === */
        if (!g_mcf_i2c_disable)
        {
            static uint32_t s_last_200 = 0;
            if ((uint32_t)(now_ms - s_last_200) >= 200U) {
                s_last_200 = now_ms;
                g_mcf_kick_rc   = (uint32_t)MCF8329A_KickWatchdog(&s_mcf);
                g_mcf_spin_rc   = (uint32_t)MCF8329A_SpinDuty(&s_mcf, g_mcf_spin_duty);
                g_mcf_status_rc = (uint32_t)MCF8329A_RefreshStatus(&s_mcf);
                g_mcf_algo_status = s_mcf.last_algo_status;
                g_mcf_gate_fault  = s_mcf.last_gate_fault;
                g_mcf_ctrl_fault  = s_mcf.last_ctrl_fault;
                g_mcf_state_rc    = (uint32_t)MCF8329A_ReadAlgoState(&s_mcf, (uint32_t *)&g_mcf_algo_state);
                /* MAX_SPEED 提升后 ramp 期间会偶发触发 ABNORMAL_SPEED/BEMF latched fault.
                 * 故障 latch 会让 chip 进 IDLE 不再启动. 这里:有 ctrl_fault 且 motor 不在
                 * CLOSED_LOOP_ALIGNED (state != 9) 时, 周期性清 fault 让它重试. */
                if (g_mcf_ctrl_fault != 0U && (g_mcf_algo_state & 0x1FU) != 0x09U) {
                    (void)MCF8329A_ClearFault(&s_mcf);
                }
                /* VM_VOLTAGE @ 0x45C — motor 母线电压 (Q-format, ~24V 应该是 0x0180_0000 量级) */
                (void)MCF8329A_Read32(&s_mcf, 0x00045CU, (uint32_t *)&g_mcf_vm_voltage);
                (void)MCF8329A_Read32(&s_mcf, 0x000460U, (uint32_t *)&g_mcf_phase_a);
                (void)MCF8329A_Read32(&s_mcf, 0x0000E8U, (uint32_t *)&g_mcf_mpet_status);
                /* FG_SPEED_FDBK @ 0x19C — FG 引脚反馈速度 */
                (void)MCF8329A_Read32(&s_mcf, 0x00019CU, (uint32_t *)&g_mcf_fg_speed);
                /* SPEED_FDBK @ 0x76E — 闭环估速 (BEMF estimator 输出) */
                (void)MCF8329A_Read32(&s_mcf, 0x00076EU, (uint32_t *)&g_mcf_speed_fdbk);
            }
        }

        /* === 1s 分流: Status 帧 === */
        {
            static uint32_t s_last_1000 = 0;
            if ((uint32_t)(now_ms - s_last_1000) >= 1000U) {
                s_last_1000 = now_ms;

                float ambient_c = SensorAcq_NTCToCelsius(SensorAcq_GetNTC(7));
                float v_24in    = INA226_GetVoltage(&sensors[2]);
                float p_24in    = INA226_GetPower(&sensors[2]);
                float v_bat     = INA226_GetVoltage(&sensors[0]);
                int16_t pct = (int16_t)((v_bat - 15.0f) / (24.8f - 15.0f) * 100.0f);
                if (pct > 100) pct = 100;
                if (pct < 0)   pct = 0;

                static uint16_t ota_ramp = 0;
                int16_t ramp_lo, ramp_span;
                if (RTC->BKP0R == 0xB0B0B0B0U) { ramp_lo = 10; ramp_span = 11; }
                else                            { ramp_lo = 1;  ramp_span = 10; }
                int16_t ramp_val = ramp_lo + (int16_t)(ota_ramp % (uint16_t)ramp_span);
                ota_ramp++;

                EspComm_Status st;
                st.water_temp_x10   = ramp_val;
                st.battery_pct      = (uint8_t)pct;
                st.total_power_w    = (uint16_t)p_24in;
                st.error_flags      = 0;
                st.ambient_temp_x10 = (int16_t)(ambient_c * 10.0f);
                (void)v_24in;

                if (!OtaProto_IsBusy()) {
                    EspComm_SendStatus(&st);
                }
            }
        }

        HAL_Delay(10);
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 4;
    osc.PLL.PLLN       = 168;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

/* HAL timer callbacks → 业务模块 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    FanCtrl_CaptureCallback(htim);
    CompressorCtrl_CaptureCallback(htim);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    FanCtrl_OverflowCallback(htim);
    CompressorCtrl_OverflowCallback(htim);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { (void)file; (void)line; }
#endif
