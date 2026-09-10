/**
 ******************************************************************************
 * @file    system_stm32f4xx.c
 * @brief   CMSIS system initialization
 *
 * ไฟล์นี้ต้องมีแค่ 3 อย่าง: SystemInit(), SystemCoreClock และตาราง prescaler
 * ห้ามใส่ interrupt handler ใด ๆ ในนี้ — handler ทั้งหมดอยู่ที่ stm32f4xx_it.c
 *
 * SystemInit() ถูกเรียกจาก Reset_Handler ก่อนเข้า main()
 * ตอนนั้น .data ยังไม่ถูก copy และ .bss ยังไม่ถูกเคลียร์
 * จึงห้ามใช้ตัวแปร global ที่มีค่าเริ่มต้นในฟังก์ชันนี้
 ******************************************************************************
 */

#include "stm32f411xe.h"

/* ตั้งค่าเริ่มต้นเป็น HSI 16 MHz ตามสภาพหลัง reset
 * SystemClock_Config() ใน main.c จะอัปเดตค่านี้เป็น 84 MHz ทีหลัง */
uint32_t SystemCoreClock = 16000000U;

const uint8_t AHBPrescTable[16] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                   1U, 2U, 3U, 4U, 6U, 7U, 8U, 9U};
const uint8_t APBPrescTable[8]  = {0U, 0U, 0U, 0U, 1U, 2U, 3U, 4U};

/**
 * @brief เตรียมระบบขั้นต่ำสุดก่อนเข้า main()
 */
void SystemInit(void)
{
#if defined(__FPU_PRESENT) && (__FPU_PRESENT == 1) && \
    defined(__FPU_USED)    && (__FPU_USED == 1)
    /* เปิดสิทธิ์เข้าถึง FPU (CP10 และ CP11) — ต้องทำก่อนใช้ float
     * โปรเจคคอมไพล์ด้วย -mfloat-abi=hard ถ้าไม่เปิดจะเกิด HardFault */
    SCB->CPACR |= ((3UL << (10U * 2U)) | (3UL << (11U * 2U)));
#endif

    /* ตาราง vector อยู่ต้น flash */
    SCB->VTOR = FLASH_BASE;
}
