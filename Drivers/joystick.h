/**
 ******************************************************************************
 * @file    joystick.h
 * @brief   Analog joystick driver (ADC1 + EOC interrupt, register level)
 *          STM32 Mini Racing Game / Nucleo-F411RE
 *
 * Layer  : Driver  — ไม่ include ไฟล์ใด ๆ จาก Application/
 * Concur : Joystick_IrqHandler() ถูกเรียกจาก ADC_IRQHandler เท่านั้น
 *          ฟังก์ชันอื่นทั้งหมดต้องเรียกจาก main loop เท่านั้น
 ******************************************************************************
 */

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief ทิศทางที่ driver แปลงจากค่า ADC แล้ว
 *        Application จะ map ค่านี้เป็น GAME_DIRECTION_* เองใน main.c
 *        (driver ไม่รู้จัก game_types.h เพื่อไม่ให้ layer ปนกัน)
 */
typedef enum
{
    JOYSTICK_CENTER = 0,
    JOYSTICK_LEFT   = 1,
    JOYSTICK_RIGHT  = 2
} Joystick_Dir_t;

/**
 * @brief เปิด clock, ตั้ง GPIO เป็น analog, ตั้ง ADC1 และเปิด EOC interrupt
 * @note  ต้องเรียกหลัง SystemClock config และก่อน Joystick_Calibrate()
 */
void Joystick_Init(void);

/**
 * @brief วัดค่ากลางของจอย (เฉลี่ยหลายครั้ง) ตอนบูต
 * @note  ฟังก์ชันนี้ block ใช้ได้เฉพาะก่อนเข้า main loop และห้ามแตะจอยขณะวัด
 *        ถ้า ADC ไม่ตอบภายใน timeout จะใช้ค่ากลางตามทฤษฎีแทนแล้ว return
 */
void Joystick_Calibrate(void);

/**
 * @brief สั่งเริ่มแปลงค่า X แล้ว Y (ไม่ block, ผลจะมาทาง interrupt)
 * @note  เรียกหนึ่งครั้งต่อ game tick จาก main loop
 *        ถ้ารอบก่อนยังไม่เสร็จ จะข้ามการสั่งครั้งนี้ไปเงียบ ๆ
 */
void Joystick_StartSample(void);

/**
 * @brief true เมื่อมีชุดค่าใหม่ที่แปลงเสร็จแล้วอย่างน้อยหนึ่งชุด
 */
bool Joystick_IsSampleReady(void);

/**
 * @brief แปลงค่าล่าสุดเป็น LEFT / CENTER / RIGHT
 * @note  มี dead zone และ hysteresis ในตัว กันทิศกระพริบตรงขอบ
 *        ถ้ายังไม่มีค่าใหม่ จะคืนผลจากค่าล่าสุดที่มีอยู่ ไม่ block
 */
Joystick_Dir_t Joystick_GetDirection(void);

/** @brief ค่า ADC ดิบ 0..4095 ของแกน X (ไว้ debug ผ่าน UART) */
uint16_t Joystick_GetRawX(void);

/** @brief ค่า ADC ดิบ 0..4095 ของแกน Y (ยังไม่ใช้ในเกม 3 เลน) */
uint16_t Joystick_GetRawY(void);

/**
 * @brief ตัวจัดการ interrupt ของ ADC1
 * @note  ให้ ADC_IRQHandler() ใน stm32f4xx_it.c เรียกฟังก์ชันนี้อย่างเดียว
 *        ห้ามเรียก game logic จากที่นี่
 */
void Joystick_IrqHandler(void);

#endif /* JOYSTICK_H */
