/**
 ******************************************************************************
 * @file    joystick.c
 * @brief   Analog joystick driver — ADC1 + EOC interrupt, register level (CMSIS)
 *          STM32 Mini Racing Game / Nucleo-F411RE
 *
 * หลักการทำงาน
 *   main loop เรียก Joystick_StartSample() หนึ่งครั้งต่อ game tick
 *   → ADC แปลงค่าแกน X → EOC interrupt → ISR เก็บค่า X แล้วสั่งแปลงแกน Y ต่อ
 *   → EOC interrupt อีกครั้ง → ISR เก็บค่า Y แล้วตั้ง flag ว่าพร้อม
 *   main loop เรียก Joystick_GetDirection() หยิบผลไปใส่ Game_Update()
 *
 *   ทั้งกระบวนการไม่มี polling loop และไม่ block main loop เลย
 *   ที่ 50 Hz จะเกิด interrupt เพียง 100 ครั้งต่อวินาที
 ******************************************************************************
 */

#include "joystick.h"
#include "stm32f411xe.h"

/* ==========================================================================
 *  CONFIG — แก้ตรงนี้เมื่อเลือกขาจริงแล้ว
 *
 *  ขาที่ต่อ ADC1 ได้บน Nucleo-F411RE:
 *    PA0=IN0  PA1=IN1  PA2=IN2* PA3=IN3* PA4=IN4  PA5=IN5* PA6=IN6  PA7=IN7
 *    PB0=IN8  PB1=IN9
 *    PC0=IN10 PC1=IN11 PC2=IN12 PC3=IN13 PC4=IN14 PC5=IN15
 *
 *    * PA2/PA3 ถูกใช้เป็น UART2 ของ ST-Link VCP อยู่แล้ว
 *    * PA5 ต่อกับ LD2 บนบอร์ด
 *    เลี่ยง 3 ขานี้ถ้ายังต้องใช้ debug UART และไฟแสดงสถานะ
 *
 *  เปลี่ยนขา = แก้ทั้ง 3 บรรทัดของแกนนั้นให้ตรงกัน (PORT / PIN / CH)
 * ========================================================================== */

/* --- แกน X (ใช้เลี้ยวซ้าย/ขวาในเกม) --- */
#define JOY_X_PORT              GPIOC
#define JOY_X_PIN               0U
#define JOY_X_CH                10U
#define JOY_X_GPIO_CLK_EN       RCC_AHB1ENR_GPIOCEN

/* --- แกน Y (ยังไม่ใช้ในเกม 3 เลน แต่อ่านเก็บไว้) --- */
#define JOY_Y_PORT              GPIOC
#define JOY_Y_PIN               1U
#define JOY_Y_CH                11U
#define JOY_Y_GPIO_CLK_EN       RCC_AHB1ENR_GPIOCEN

/* --- การแปลงค่าเป็นทิศทาง --- */
#define JOY_ADC_CENTER          2048U   /* ค่ากลางตามทฤษฎี ใช้เมื่อ calibrate ไม่สำเร็จ */
#define JOY_THRESHOLD_ENTER     700     /* เกินเท่านี้จึงเริ่มนับว่าโยก */
#define JOY_THRESHOLD_EXIT      450     /* ต่ำกว่านี้จึงกลับเป็น CENTER (hysteresis) */
#define JOY_INVERT_X            0       /* ตั้งเป็น 1 ถ้าโยกขวาแล้วได้ LEFT */

/* --- อื่น ๆ --- */
#define JOY_CAL_SAMPLES         32U
#define JOY_CAL_TIMEOUT         200000U /* วนรอเปล่ากี่รอบจึงยอมแพ้ */
#define JOY_IRQ_PRIORITY        6U      /* ต่ำกว่า game tick timer เสมอ */

/* ==========================================================================
 *  บาง CMSIS header เรียก ADC common register ว่า ADC123_COMMON
 * ========================================================================== */
#if !defined(ADC1_COMMON)
#define ADC1_COMMON             ADC123_COMMON
#endif

/* ==========================================================================
 *  Module state
 *  ตัวแปรที่ ISR เขียนต้องเป็น volatile ทุกตัว
 *  ขนาด 16/8 บิตบน Cortex-M4 อ่าน/เขียนได้ในคำสั่งเดียว จึงไม่ต้องปิด interrupt
 * ========================================================================== */
static volatile uint16_t joy_raw_x   = JOY_ADC_CENTER;
static volatile uint16_t joy_raw_y   = JOY_ADC_CENTER;
static volatile uint8_t  joy_seq_idx = 0U;   /* 0 = กำลังแปลง X, 1 = กำลังแปลง Y */
static volatile bool     joy_busy    = false;
static volatile bool     joy_ready   = false;

/* ตัวแปรฝั่ง main loop เท่านั้น ไม่ต้อง volatile */
static uint16_t       joy_center_x = JOY_ADC_CENTER;
static uint16_t       joy_center_y = JOY_ADC_CENTER;
static Joystick_Dir_t joy_last_dir = JOYSTICK_CENTER;

/* ==========================================================================
 *  Private helpers
 * ========================================================================== */

/**
 * @brief ตั้งขา GPIO เป็น analog mode (MODER = 0b11, ไม่มี pull)
 */
static void Joystick_ConfigAnalogPin(GPIO_TypeDef *port, uint32_t pin)
{
    port->MODER |= (3UL << (pin * 2U));
    port->PUPDR &= ~(3UL << (pin * 2U));
}

/**
 * @brief ตั้ง sampling time = 84 cycles (0b100) ให้ channel ที่ระบุ
 *        จอยเป็นตัวต้านทานปรับค่า impedance สูง ต้อง sample ยาวหน่อยค่าจึงนิ่ง
 */
static void Joystick_ConfigSampleTime(uint32_t channel)
{
    if (channel >= 10U)
    {
        ADC1->SMPR1 &= ~(7UL << ((channel - 10U) * 3U));
        ADC1->SMPR1 |=  (4UL << ((channel - 10U) * 3U));
    }
    else
    {
        ADC1->SMPR2 &= ~(7UL << (channel * 3U));
        ADC1->SMPR2 |=  (4UL << (channel * 3U));
    }
}

/**
 * @brief หน่วงสั้น ๆ แบบวนเปล่า ใช้เฉพาะรอ ADC stabilize ตอน init
 */
static void Joystick_ShortDelay(void)
{
    volatile uint32_t i;
    for (i = 0U; i < 1000U; i++)
    {
        /* busy wait ~ไม่กี่สิบไมโครวินาที */
    }
}

/* ==========================================================================
 *  Public API
 * ========================================================================== */

void Joystick_Init(void)
{
    /* 1) เปิด clock ของ GPIO และ ADC1 */
    RCC->AHB1ENR |= (JOY_X_GPIO_CLK_EN | JOY_Y_GPIO_CLK_EN);
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    /* 2) ขาจอยเป็น analog input */
    Joystick_ConfigAnalogPin(JOY_X_PORT, JOY_X_PIN);
    Joystick_ConfigAnalogPin(JOY_Y_PORT, JOY_Y_PIN);

    /* 3) ADC clock prescaler: PCLK2 / 4
     *    ถ้า SYSCLK = 84 MHz และ APB2 = 84 MHz จะได้ ADCCLK = 21 MHz (ไม่เกิน 36 MHz) */
    ADC1_COMMON->CCR &= ~ADC_CCR_ADCPRE;
    ADC1_COMMON->CCR |=  (1UL << ADC_CCR_ADCPRE_Pos);

    /* 4) CR1: resolution 12-bit, ไม่ใช้ scan mode, เปิด EOC interrupt */
    ADC1->CR1  = 0UL;
    ADC1->CR1 |= ADC_CR1_EOCIE;

    /* 5) CR2: ข้อมูลชิดขวา, single conversion, trigger ด้วย software */
    ADC1->CR2  = 0UL;

    /* 6) ลำดับการแปลง: 1 conversion ต่อครั้ง (L = 0) */
    ADC1->SQR1 = 0UL;
    ADC1->SQR3 = (uint32_t)JOY_X_CH;

    /* 7) sampling time ของทั้งสอง channel */
    Joystick_ConfigSampleTime((uint32_t)JOY_X_CH);
    Joystick_ConfigSampleTime((uint32_t)JOY_Y_CH);

    /* 8) เปิด ADC แล้วรอ stabilize */
    ADC1->CR2 |= ADC_CR2_ADON;
    Joystick_ShortDelay();

    /* 9) ล้าง flag ค้างแล้วเปิด interrupt ที่ NVIC */
    ADC1->SR = 0UL;
    NVIC_SetPriority(ADC_IRQn, JOY_IRQ_PRIORITY);
    NVIC_EnableIRQ(ADC_IRQn);

    joy_busy    = false;
    joy_ready   = false;
    joy_seq_idx = 0U;
}

void Joystick_Calibrate(void)
{
    uint32_t sum_x   = 0U;
    uint32_t sum_y   = 0U;
    uint32_t taken   = 0U;
    uint32_t guard   = 0U;
    bool     ok      = true;

    while ((taken < JOY_CAL_SAMPLES) && ok)
    {
        Joystick_StartSample();

        guard = 0U;
        while ((!joy_ready) && (guard < JOY_CAL_TIMEOUT))
        {
            guard++;
        }

        if (guard >= JOY_CAL_TIMEOUT)
        {
            ok = false;    /* ADC ไม่ตอบ — เช็ค clock, NVIC หรือ ADC_IRQHandler */
        }
        else
        {
            joy_ready = false;
            sum_x += (uint32_t)joy_raw_x;
            sum_y += (uint32_t)joy_raw_y;
            taken++;
        }
    }

    if (ok)
    {
        joy_center_x = (uint16_t)(sum_x / JOY_CAL_SAMPLES);
        joy_center_y = (uint16_t)(sum_y / JOY_CAL_SAMPLES);
    }
    else
    {
        joy_center_x = (uint16_t)JOY_ADC_CENTER;
        joy_center_y = (uint16_t)JOY_ADC_CENTER;
    }

    joy_last_dir = JOYSTICK_CENTER;
}

void Joystick_StartSample(void)
{
    if (!joy_busy)
    {
        joy_busy    = true;
        joy_seq_idx = 0U;
        ADC1->SQR3  = (uint32_t)JOY_X_CH;
        ADC1->CR2  |= ADC_CR2_SWSTART;
    }
    /* ถ้ายัง busy อยู่ = รอบก่อนยังไม่จบ ข้ามไปเงียบ ๆ ไม่ block main loop */
}

bool Joystick_IsSampleReady(void)
{
    return joy_ready;
}

Joystick_Dir_t Joystick_GetDirection(void)
{
    int32_t        dx;
    Joystick_Dir_t dir;

    joy_ready = false;

    dx = (int32_t)joy_raw_x - (int32_t)joy_center_x;

#if (JOY_INVERT_X != 0)
    dx = -dx;
#endif

    dir = joy_last_dir;

    /* hysteresis: เข้ายาก ออกง่าย กันทิศกระพริบตอนโยกค้างใกล้เส้นแบ่ง */
    if (dir == JOYSTICK_CENTER)
    {
        if (dx >= (int32_t)JOY_THRESHOLD_ENTER)
        {
            dir = JOYSTICK_RIGHT;
        }
        else if (dx <= -(int32_t)JOY_THRESHOLD_ENTER)
        {
            dir = JOYSTICK_LEFT;
        }
        else
        {
            /* ยังอยู่ในโซนตาย */
        }
    }
    else if (dir == JOYSTICK_RIGHT)
    {
        if (dx < (int32_t)JOY_THRESHOLD_EXIT)
        {
            dir = JOYSTICK_CENTER;
        }
    }
    else
    {
        if (dx > -(int32_t)JOY_THRESHOLD_EXIT)
        {
            dir = JOYSTICK_CENTER;
        }
    }

    joy_last_dir = dir;

    return dir;
}

uint16_t Joystick_GetRawX(void)
{
    return joy_raw_x;
}

uint16_t Joystick_GetRawY(void)
{
    return joy_raw_y;
}

/* ==========================================================================
 *  Interrupt context — สั้นที่สุด ไม่มี loop ไม่มีการเรียก game logic
 * ========================================================================== */

void Joystick_IrqHandler(void)
{
    uint16_t value;

    /* overrun: ล้างทิ้งแล้วปล่อยรอบนี้ไป รอบหน้าเริ่มใหม่ */
    if ((ADC1->SR & ADC_SR_OVR) != 0UL)
    {
        ADC1->SR &= ~ADC_SR_OVR;
        joy_busy  = false;
    }

    if ((ADC1->SR & ADC_SR_EOC) != 0UL)
    {
        value = (uint16_t)(ADC1->DR & 0x0FFFUL);   /* อ่าน DR แล้ว EOC เคลียร์เอง */

        if (joy_seq_idx == 0U)
        {
            joy_raw_x   = value;
            joy_seq_idx = 1U;
            ADC1->SQR3  = (uint32_t)JOY_Y_CH;      /* สลับไปแกน Y */
            ADC1->CR2  |= ADC_CR2_SWSTART;
        }
        else
        {
            joy_raw_y = value;
            joy_ready = true;
            joy_busy  = false;                     /* จบหนึ่งชุด */
        }
    }
}
