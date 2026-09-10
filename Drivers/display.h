/**
 ******************************************************************************
 * @file    display.h
 * @brief   Monochrome OLED driver 128x64 (SH1106 / SSD1306) — I2C1 + DMA
 *          STM32 Mini Racing Game / Nucleo-F411RE
 *
 * Layer  : Driver — ไม่ include ไฟล์ใด ๆ จาก Application/
 *          รู้จักแค่ pixel / rect / text ไม่รู้จักเลน รถ หรือคะแนน
 *          การวาดเกมจริงเป็นหน้าที่ของ Application/game_render.c
 *
 * Concur : Display_DmaIrqHandler(), Display_I2cEventIrqHandler() และ
 *          Display_I2cErrorIrqHandler() เรียกจาก stm32f4xx_it.c เท่านั้น
 *          ฟังก์ชันอื่นทั้งหมดเรียกจาก main loop เท่านั้น
 *
 * โมเดลการใช้งาน (buffer เดียว ไม่มี double buffer)
 *   1. main loop เช็ค Display_IsBusy() ถ้า true แปลว่า DMA กำลังอ่าน buffer อยู่
 *      ให้ข้ามการวาดรอบนี้ไป ห้ามแตะ buffer เด็ดขาด
 *   2. ถ้าไม่ busy: Display_Clear() -> วาด -> Display_Flush()
 *   3. Display_Flush() คืนทันที ภาพจะทยอยส่งเองผ่าน DMA + interrupt
 *
 *   ที่ 400 kHz หนึ่งเฟรมใช้เวลาราว 24 ms (มากกว่า 1 tick 20 ms)
 *   จึงได้ประมาณ 25 fps โดย CPU แทบไม่ต้องทำอะไรระหว่างนั้น
 ******************************************************************************
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/* ==========================================================================
 *  ขนาดจอและฟอนต์
 * ========================================================================== */
#define DISPLAY_WIDTH           (128U)
#define DISPLAY_HEIGHT          (64U)
#define DISPLAY_PAGES           (DISPLAY_HEIGHT / 8U)

/** ความกว้างที่ตัวอักษรหนึ่งตัวกินรวมช่องว่าง (glyph 5 px + spacing 1 px) */
#define DISPLAY_FONT_WIDTH      (6U)
/** ความสูงที่ตัวอักษรหนึ่งตัวกินรวมช่องว่าง (glyph 7 px + spacing 1 px) */
#define DISPLAY_FONT_HEIGHT     (8U)

/**
 * @brief สีของ pixel บนจอขาวดำ
 * @note  DISPLAY_INVERT สลับค่าเดิม ใช้ทำ highlight เมนูได้โดยไม่ต้องวาดสองรอบ
 */
typedef enum
{
    DISPLAY_BLACK  = 0,
    DISPLAY_WHITE  = 1,
    DISPLAY_INVERT = 2
} Display_Color_t;

/* ==========================================================================
 *  Setup — เรียกครั้งเดียวก่อนเข้า main loop
 * ========================================================================== */

/**
 * @brief เปิด clock, ตั้ง GPIO/I2C1/DMA, กู้บัสถ้าค้าง แล้วส่งชุดคำสั่ง init ให้จอ
 * @retval true  จอตอบ ACK และ init สำเร็จ
 * @retval false จอไม่ตอบ (ไม่ได้เสียบ, address ผิด, หรือบัสค้าง)
 * @note  ฟังก์ชันนี้ block ราว 30 ms ใช้ได้เฉพาะก่อนเข้า main loop
 *        ถ้าคืน false ฟังก์ชันวาดยังเรียกได้ตามปกติ แต่ Flush จะไม่ทำอะไร
 *        เกมจึงเดินต่อได้แม้ถอดจอออก
 */
bool Display_Init(void);

/** @brief true ถ้า Display_Init() เจอจอ */
bool Display_IsPresent(void);

/**
 * @brief ปรับความสว่าง 0..255
 * @note  block ประมาณ 60 us ห้ามเรียกขณะ Display_IsBusy() เป็น true
 */
void Display_SetContrast(uint8_t contrast);

/* ==========================================================================
 *  การวาด — แก้แค่ buffer ใน RAM ยังไม่ส่งออกจอ
 *  ทุกฟังก์ชัน clip นอกขอบจอให้เอง ส่งพิกัดติดลบหรือเกินขอบได้ ไม่พัง
 * ========================================================================== */

/** @brief ล้าง buffer เป็นสีดำทั้งหมด */
void Display_Clear(void);

/** @brief ถมทั้งจอด้วยสีที่ระบุ */
void Display_Fill(Display_Color_t color);

void Display_DrawPixel(int16_t x, int16_t y, Display_Color_t color);
void Display_DrawHLine(int16_t x, int16_t y, int16_t width, Display_Color_t color);
void Display_DrawVLine(int16_t x, int16_t y, int16_t height, Display_Color_t color);

/** @brief สี่เหลี่ยมทึบ — primitive หลักที่เกมใช้วาดรถและสิ่งกีดขวาง */
void Display_FillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                      Display_Color_t color);

/** @brief สี่เหลี่ยมขอบหนา 1 px */
void Display_DrawRect(int16_t x, int16_t y, int16_t width, int16_t height,
                      Display_Color_t color);

/**
 * @brief วาด bitmap ขาวดำ เรียงตามแถว บิตซ้ายสุดคือ MSB ของไบต์แรกของแถว
 *        แต่ละแถวเริ่มไบต์ใหม่เสมอ (ปัดขึ้นเป็น (width + 7) / 8 ไบต์ต่อแถว)
 * @note  บิต 0 ถือว่าโปร่งใส ไม่ลบพื้นหลัง เหมาะกับ sprite รถ
 */
void Display_DrawBitmap(int16_t x, int16_t y, uint8_t width, uint8_t height,
                        const uint8_t *bitmap, Display_Color_t color);

/**
 * @brief วาดตัวอักษรหนึ่งตัวด้วยฟอนต์ 5x7 (ASCII 0x20..0x7E)
 * @note  วาดเฉพาะจุดที่ติด พื้นหลังโปร่งใส ตัวอักษรนอกช่วงจะกลายเป็น '?'
 */
void Display_DrawChar(int16_t x, int16_t y, char character, Display_Color_t color);

/** @brief วาดสตริงตามแนวนอน ไม่ตัดบรรทัดให้อัตโนมัติ */
void Display_DrawText(int16_t x, int16_t y, const char *text, Display_Color_t color);

/** @brief วาดเลขฐานสิบไม่มีเครื่องหมาย ไม่เติมศูนย์นำหน้า */
void Display_DrawNumber(int16_t x, int16_t y, uint32_t value, Display_Color_t color);

/**
 * @brief ความกว้างเป็น pixel ที่สตริงจะกินจริง (ไม่นับช่องว่างท้ายตัวสุดท้าย)
 * @note  ใช้จัดข้อความกลางจอ: x = (DISPLAY_WIDTH - Display_TextWidth(s)) / 2
 */
uint16_t Display_TextWidth(const char *text);

/* ==========================================================================
 *  การส่งภาพออกจอ
 * ========================================================================== */

/**
 * @brief เริ่มส่ง buffer ทั้งเฟรมออกจอผ่าน DMA (ไม่ block)
 * @retval true  เริ่มส่งแล้ว
 * @retval false รอบก่อนยังไม่เสร็จ หรือไม่มีจอ — ข้ามเฟรมนี้ไป
 * @note  ห้ามแก้ buffer จนกว่า Display_IsBusy() จะกลับเป็น false
 *        ถ้าเรียกซ้ำขณะ busy ติดกันนาน ๆ (จอหลุดกลางคัน) จะยกเลิกรอบค้างให้เอง
 */
bool Display_Flush(void);

/** @brief true ระหว่างที่ DMA ยังอ่าน buffer อยู่ ห้ามวาดทับ */
bool Display_IsBusy(void);

/**
 * @brief ส่งทั้งเฟรมแบบ block จนเสร็จ
 * @note  block ราว 25 ms ที่ 400 kHz ใช้เฉพาะตอน boot หรือเวลาไล่ปัญหา
 *        ห้ามใช้ใน main loop ของเกมเพราะกิน tick เกิน
 */
bool Display_FlushBlocking(void);

/** @brief จำนวนครั้งที่การส่งล้มเหลว (NACK / bus error / DMA error) ไว้ debug ผ่าน UART */
uint16_t Display_GetErrorCount(void);

/* ==========================================================================
 *  Interrupt context — ให้ stm32f4xx_it.c เรียกเท่านั้น
 * ========================================================================== */

/** @brief ให้ DMA1_Stream6_IRQHandler() เรียก */
void Display_DmaIrqHandler(void);

/** @brief ให้ I2C1_EV_IRQHandler() เรียก */
void Display_I2cEventIrqHandler(void);

/** @brief ให้ I2C1_ER_IRQHandler() เรียก */
void Display_I2cErrorIrqHandler(void);

#endif /* DISPLAY_H */
