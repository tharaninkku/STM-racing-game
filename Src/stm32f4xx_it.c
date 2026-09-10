/**
 ******************************************************************************
 * @file    stm32f4xx_it.c
 * @brief   Interrupt service routines
 *
 * กติกาของโปรเจคนี้: ISR แค่รับข้อมูลหรือแจ้งเหตุการณ์เท่านั้น
 * ห้ามเรียก game logic จากในนี้เด็ดขาด
 ******************************************************************************
 */

#include "stm32f411xe.h"
#include "joystick.h"

/* นับ tick ที่ยังไม่ได้ประมวลผล ประกาศไว้ใน main.c */
extern volatile uint32_t tick_pending;

/* ==========================================================================
 *  Cortex-M4 core exceptions
 * ========================================================================== */

void NMI_Handler(void)
{
    while (1)
    {
    }
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/**
 * @brief game tick ทุก 20 ms
 * @note  เพิ่ม counter อย่างเดียว งานจริงทำใน main loop
 */
void SysTick_Handler(void)
{
    tick_pending++;
}

/* ==========================================================================
 *  Peripheral interrupts
 * ========================================================================== */

/**
 * @brief ADC1 end-of-conversion
 * @note  ถ้าลืมบรรทัดนี้ Joystick_Calibrate() จะรอจนหมด timeout
 *        แล้วใช้ค่ากลาง 2048 แทน — อาการคือจอยไม่ตอบสนองเลย
 */
void ADC_IRQHandler(void)
{
    Joystick_IrqHandler();
}
