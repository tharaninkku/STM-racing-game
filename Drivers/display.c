/**
 ******************************************************************************
 * @file    display.c
 * @brief   Monochrome OLED 128x64 (SH1106 / SSD1306) — I2C1 + DMA, register level
 *          STM32 Mini Racing Game / Nucleo-F411RE
 *
 * ทำไมต้องใช้ DMA
 *   หนึ่งเฟรมคือ 128 x 64 บิต = 1024 ไบต์ บวก header อีกหน้าละ 7 ไบต์
 *   ที่ I2C 400 kHz (9 บิตต่อไบต์รวม ACK) ใช้เวลาราว 24 ms
 *   ซึ่ง "มากกว่า" หนึ่ง game tick (20 ms) ถ้าเขียนแบบ blocking เกมจะกระตุกทันที
 *   ที่นี่จึงส่งด้วย DMA1 Stream6 แล้วปล่อยให้ interrupt เดินเรื่องต่อเอง
 *   main loop เสียเวลาแค่ตอนสั่งเริ่มเท่านั้น
 *
 * ลำดับการส่งหนึ่งหน้า (หนึ่ง page = 8 แถว)
 *   START -> address -> [DMA ยิง 135 ไบต์] -> BTF -> repeated START หน้าถัดไป
 *   หน้าสุดท้ายจึงค่อย STOP
 *
 *   ทั้งเฟรมใช้ transaction เดียวต่อเนื่องด้วย repeated start จึงไม่ต้องรอ
 *   bus free ระหว่างหน้า — สำคัญมาก เพราะการรอแบบนั้นคือ loop ใน ISR
 *   ซึ่งผิดกติกาของโปรเจคนี้
 *
 *   ผลข้างเคียง: ระหว่าง flush (~24 ms) บัส I2C1 ถูกจองทั้งเส้น
 *   ถ้าภายหลังจะใช้ BH1750 / AHT10 บน shield ที่ขาเดียวกัน
 *   ต้องอ่าน sensor ตอน Display_IsBusy() เป็น false เท่านั้น
 *
 * โครงของ buffer
 *   หน้าแต่ละหน้าเก็บ header ของตัวเองไว้หน้าข้อมูลเลย DMA จึงยิงรวดเดียวได้
 *   ไม่ต้องแยกส่งคำสั่งกับข้อมูล:
 *
 *     [0]=0x80 [1]=0xB0|page   ตั้งเลขหน้า
 *     [2]=0x80 [3]=0x00|col_lo ตั้งคอลัมน์ 4 บิตล่าง
 *     [4]=0x80 [5]=0x10|col_hi ตั้งคอลัมน์ 4 บิตบน
 *     [6]=0x40                 ต่อจากนี้เป็นข้อมูลล้วน
 *     [7..134]                 pixel 128 คอลัมน์ของหน้านี้
 *
 *   0x80 คือ control byte แบบ Co=1 (ตามด้วยคำสั่งหนึ่งไบต์)
 *   0x40 คือ Co=0, D/C=1 (ที่เหลือเป็นข้อมูลทั้งหมด)
 ******************************************************************************
 */

#include "display.h"
#include "stm32f411xe.h"
#include <stddef.h>

/* ==========================================================================
 *  CONFIG — แก้ตรงนี้ที่เดียวเวลาเปลี่ยนจอหรือเปลี่ยนขา
 * ========================================================================== */

/* --- ชนิด controller ---
 *   จอ 1.3 นิ้ว "IIC V2.2" เกือบทั้งหมดเป็น SH1106
 *   จอ 0.96 นิ้วเป็น SSD1306
 *
 *   อาการที่บอกว่าเลือกผิด: ภาพเลื่อนไปทางขวา 2 pixel และขอบซ้าย 2 คอลัมน์
 *   เป็นขยะ -> ตอนนี้ตั้งเป็น SSD1306 แต่จอจริงเป็น SH1106
 *   ถ้าภาพเลื่อนไปทางซ้ายและขอบขวาขาด -> ตั้งเป็น SH1106 แต่จอจริงเป็น SSD1306
 */
#define DISP_SH1106             (1)
#define DISP_SSD1306            (2)
#define DISP_CONTROLLER         DISP_SH1106

/* --- I2C --- */
#define DISP_I2C                I2C1
#define DISP_I2C_CLK_EN         RCC_APB1ENR_I2C1EN
#define DISP_I2C_EV_IRQn        I2C1_EV_IRQn
#define DISP_I2C_ER_IRQn        I2C1_ER_IRQn

/** 7-bit address ของจอ ส่วนใหญ่เป็น 0x3C ถ้าย้ายจัมเปอร์บนโมดูลจะเป็น 0x3D */
#define DISP_ADDR               (0x3CU)

/** ไม่ควรเกิน 400 kHz ตาม datasheet ของทั้ง SH1106 และ SSD1306 */
#define DISP_I2C_SPEED_HZ       (400000UL)

/** ต้องตรงกับ APB1 จริง (SYSCLK 84 MHz -> APB1 42 MHz) */
#define DISP_PCLK1_HZ           (42000000UL)

/* --- ขา ---
 *   PB8 = I2C1_SCL, PB9 = I2C1_SDA (AF4)
 *   เป็นขาเดียวกับที่ STEO Training Shield 1 ใช้ต่อ BH1750 / AHT10
 *   จึงมี pull-up บน shield ให้แล้ว และ address 0x3C ไม่ชนกับ 0x23 / 0x38
 */
#define DISP_SCL_PORT           GPIOB
#define DISP_SCL_PIN            (8U)
#define DISP_SDA_PORT           GPIOB
#define DISP_SDA_PIN            (9U)
#define DISP_GPIO_CLK_EN        RCC_AHB1ENR_GPIOBEN
#define DISP_GPIO_AF            (4U)

/* --- DMA ---
 *   DMA1 Stream6 Channel1 = I2C1_TX บน STM32F411 (ดู RM0383 ตาราง DMA1 request)
 *   ถ้าเปลี่ยน stream ต้องแก้ทั้ง 6 บรรทัดนี้ให้ตรงกัน (HISR/HIFCR เป็นของ stream 4..7)
 */
#define DISP_DMA                DMA1
#define DISP_DMA_STREAM         DMA1_Stream6
#define DISP_DMA_CHANNEL        (1UL)
#define DISP_DMA_IRQn           DMA1_Stream6_IRQn
#define DISP_DMA_CLK_EN         RCC_AHB1ENR_DMA1EN
#define DISP_DMA_ISR()          (DISP_DMA->HISR)
#define DISP_DMA_TCIF           DMA_HISR_TCIF6
#define DISP_DMA_TEIF           (DMA_HISR_TEIF6 | DMA_HISR_DMEIF6)
#define DISP_DMA_CLEAR_FLAGS()  (DISP_DMA->HIFCR = (DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | \
                                                    DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | \
                                                    DMA_HIFCR_CFEIF6))

/* --- อื่น ๆ --- */
#define DISP_CONTRAST_DEFAULT   (0x7FU)
#define DISP_FLIP_180           (0)       /* 1 = หมุนภาพ 180 องศาเมื่อวางจอกลับด้าน */
#define DISP_IRQ_PRIORITY       (7U)      /* ต่ำกว่า game tick timer และ ADC (6) เสมอ */
#define DISP_BLOCK_TIMEOUT      (200000UL)/* วนรอเปล่ากี่รอบจึงยอมแพ้ ใช้เฉพาะตอน init */
#define DISP_STALL_LIMIT        (10U)     /* busy ค้างกี่ tick จึงยกเลิกรอบนั้นทิ้ง */

/* ==========================================================================
 *  ค่าที่คำนวณจาก CONFIG
 * ========================================================================== */

/* SH1106 มี RAM 132 คอลัมน์แต่จอกว้าง 128 ภาพจึงอยู่กลางที่คอลัมน์ 2..129 */
#if (DISP_CONTROLLER == DISP_SH1106)
#define DISP_COL_OFFSET         (2U)
#else
#define DISP_COL_OFFSET         (0U)
#endif

#define DISP_PCLK1_MHZ          (DISP_PCLK1_HZ / 1000000UL)

#if (DISP_I2C_SPEED_HZ > 100000UL)
/* Fast mode, duty 2:1 -> คาบทั้งลูก = 3 x CCR x TPCLK1 */
#define DISP_I2C_CCR            (I2C_CCR_FS | (DISP_PCLK1_HZ / (3UL * DISP_I2C_SPEED_HZ)))
#define DISP_I2C_TRISE          (((DISP_PCLK1_MHZ * 300UL) / 1000UL) + 1UL)
#else
/* Standard mode -> คาบทั้งลูก = 2 x CCR x TPCLK1 */
#define DISP_I2C_CCR            (DISP_PCLK1_HZ / (2UL * DISP_I2C_SPEED_HZ))
#define DISP_I2C_TRISE          (DISP_PCLK1_MHZ + 1UL)
#endif

/** header 7 ไบต์ + ข้อมูล 128 ไบต์ = ขนาดที่ DMA ยิงต่อหนึ่งหน้า */
#define DISP_PAGE_HEADER        (7U)
#define DISP_PAGE_TX_LEN        (DISP_PAGE_HEADER + DISPLAY_WIDTH)

#define DISP_FONT_FIRST         (0x20U)
#define DISP_FONT_LAST          (0x7EU)
#define DISP_FONT_GLYPH_WIDTH   (5U)
#define DISP_FONT_GLYPH_HEIGHT  (7U)
#define DISP_FONT_COUNT         ((DISP_FONT_LAST - DISP_FONT_FIRST) + 1U)

/* ==========================================================================
 *  Module state
 * ========================================================================== */

/** framebuffer พร้อม header ต่อหน้า — DMA อ่านจากตรงนี้โดยตรง */
static uint8_t disp_fb[DISPLAY_PAGES][DISP_PAGE_TX_LEN];

/* ตัวแปรที่ ISR เขียน ต้อง volatile ทุกตัว */
static volatile bool     disp_busy   = false;
static volatile uint8_t  disp_page   = 0U;
static volatile uint16_t disp_errors = 0U;

/* ตัวแปรฝั่ง main loop เท่านั้น */
static bool    disp_present = false;
static uint8_t disp_stall   = 0U;

/* ==========================================================================
 *  ฟอนต์ 5x7 — หนึ่งไบต์คือหนึ่งคอลัมน์ บิต 0 อยู่แถวบนสุด
 *  ครอบคลุม ASCII 0x20 (space) ถึง 0x7E (~) รวม 95 ตัว = 475 ไบต์ใน flash
 * ========================================================================== */
static const uint8_t disp_font[DISP_FONT_COUNT][DISP_FONT_GLYPH_WIDTH] =
{
    { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U },  /*   */
    { 0x00U, 0x00U, 0x5FU, 0x00U, 0x00U },  /* ! */
    { 0x00U, 0x07U, 0x00U, 0x07U, 0x00U },  /* " */
    { 0x14U, 0x7FU, 0x14U, 0x7FU, 0x14U },  /* # */
    { 0x24U, 0x2AU, 0x7FU, 0x2AU, 0x12U },  /* $ */
    { 0x23U, 0x13U, 0x08U, 0x64U, 0x62U },  /* % */
    { 0x36U, 0x49U, 0x55U, 0x22U, 0x50U },  /* & */
    { 0x00U, 0x05U, 0x03U, 0x00U, 0x00U },  /* ' */
    { 0x00U, 0x1CU, 0x22U, 0x41U, 0x00U },  /* ( */
    { 0x00U, 0x41U, 0x22U, 0x1CU, 0x00U },  /* ) */
    { 0x14U, 0x08U, 0x3EU, 0x08U, 0x14U },  /* * */
    { 0x08U, 0x08U, 0x3EU, 0x08U, 0x08U },  /* + */
    { 0x00U, 0x50U, 0x30U, 0x00U, 0x00U },  /* , */
    { 0x08U, 0x08U, 0x08U, 0x08U, 0x08U },  /* - */
    { 0x00U, 0x60U, 0x60U, 0x00U, 0x00U },  /* . */
    { 0x20U, 0x10U, 0x08U, 0x04U, 0x02U },  /* / */
    { 0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU },  /* 0 */
    { 0x00U, 0x42U, 0x7FU, 0x40U, 0x00U },  /* 1 */
    { 0x42U, 0x61U, 0x51U, 0x49U, 0x46U },  /* 2 */
    { 0x21U, 0x41U, 0x45U, 0x4BU, 0x31U },  /* 3 */
    { 0x18U, 0x14U, 0x12U, 0x7FU, 0x10U },  /* 4 */
    { 0x27U, 0x45U, 0x45U, 0x45U, 0x39U },  /* 5 */
    { 0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U },  /* 6 */
    { 0x01U, 0x71U, 0x09U, 0x05U, 0x03U },  /* 7 */
    { 0x36U, 0x49U, 0x49U, 0x49U, 0x36U },  /* 8 */
    { 0x06U, 0x49U, 0x49U, 0x29U, 0x1EU },  /* 9 */
    { 0x00U, 0x36U, 0x36U, 0x00U, 0x00U },  /* : */
    { 0x00U, 0x56U, 0x36U, 0x00U, 0x00U },  /* ; */
    { 0x00U, 0x08U, 0x14U, 0x22U, 0x41U },  /* < */
    { 0x14U, 0x14U, 0x14U, 0x14U, 0x14U },  /* = */
    { 0x41U, 0x22U, 0x14U, 0x08U, 0x00U },  /* > */
    { 0x02U, 0x01U, 0x51U, 0x09U, 0x06U },  /* ? */
    { 0x32U, 0x49U, 0x79U, 0x41U, 0x3EU },  /* @ */
    { 0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU },  /* A */
    { 0x7FU, 0x49U, 0x49U, 0x49U, 0x36U },  /* B */
    { 0x3EU, 0x41U, 0x41U, 0x41U, 0x22U },  /* C */
    { 0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU },  /* D */
    { 0x7FU, 0x49U, 0x49U, 0x49U, 0x41U },  /* E */
    { 0x7FU, 0x09U, 0x09U, 0x09U, 0x01U },  /* F */
    { 0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU },  /* G */
    { 0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU },  /* H */
    { 0x00U, 0x41U, 0x7FU, 0x41U, 0x00U },  /* I */
    { 0x20U, 0x40U, 0x41U, 0x3FU, 0x01U },  /* J */
    { 0x7FU, 0x08U, 0x14U, 0x22U, 0x41U },  /* K */
    { 0x7FU, 0x40U, 0x40U, 0x40U, 0x40U },  /* L */
    { 0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU },  /* M */
    { 0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU },  /* N */
    { 0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU },  /* O */
    { 0x7FU, 0x09U, 0x09U, 0x09U, 0x06U },  /* P */
    { 0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU },  /* Q */
    { 0x7FU, 0x09U, 0x19U, 0x29U, 0x46U },  /* R */
    { 0x46U, 0x49U, 0x49U, 0x49U, 0x31U },  /* S */
    { 0x01U, 0x01U, 0x7FU, 0x01U, 0x01U },  /* T */
    { 0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU },  /* U */
    { 0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU },  /* V */
    { 0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU },  /* W */
    { 0x63U, 0x14U, 0x08U, 0x14U, 0x63U },  /* X */
    { 0x07U, 0x08U, 0x70U, 0x08U, 0x07U },  /* Y */
    { 0x61U, 0x51U, 0x49U, 0x45U, 0x43U },  /* Z */
    { 0x00U, 0x7FU, 0x41U, 0x41U, 0x00U },  /* [ */
    { 0x02U, 0x04U, 0x08U, 0x10U, 0x20U },  /* \ */
    { 0x00U, 0x41U, 0x41U, 0x7FU, 0x00U },  /* ] */
    { 0x04U, 0x02U, 0x01U, 0x02U, 0x04U },  /* ^ */
    { 0x40U, 0x40U, 0x40U, 0x40U, 0x40U },  /* _ */
    { 0x00U, 0x01U, 0x02U, 0x04U, 0x00U },  /* ` */
    { 0x20U, 0x54U, 0x54U, 0x54U, 0x78U },  /* a */
    { 0x7FU, 0x48U, 0x44U, 0x44U, 0x38U },  /* b */
    { 0x38U, 0x44U, 0x44U, 0x44U, 0x20U },  /* c */
    { 0x38U, 0x44U, 0x44U, 0x48U, 0x7FU },  /* d */
    { 0x38U, 0x54U, 0x54U, 0x54U, 0x18U },  /* e */
    { 0x08U, 0x7EU, 0x09U, 0x01U, 0x02U },  /* f */
    { 0x0CU, 0x52U, 0x52U, 0x52U, 0x3EU },  /* g */
    { 0x7FU, 0x08U, 0x04U, 0x04U, 0x78U },  /* h */
    { 0x00U, 0x44U, 0x7DU, 0x40U, 0x00U },  /* i */
    { 0x20U, 0x40U, 0x44U, 0x3DU, 0x00U },  /* j */
    { 0x7FU, 0x10U, 0x28U, 0x44U, 0x00U },  /* k */
    { 0x00U, 0x41U, 0x7FU, 0x40U, 0x00U },  /* l */
    { 0x7CU, 0x04U, 0x18U, 0x04U, 0x78U },  /* m */
    { 0x7CU, 0x08U, 0x04U, 0x04U, 0x78U },  /* n */
    { 0x38U, 0x44U, 0x44U, 0x44U, 0x38U },  /* o */
    { 0x7CU, 0x14U, 0x14U, 0x14U, 0x08U },  /* p */
    { 0x08U, 0x14U, 0x14U, 0x18U, 0x7CU },  /* q */
    { 0x7CU, 0x08U, 0x04U, 0x04U, 0x08U },  /* r */
    { 0x48U, 0x54U, 0x54U, 0x54U, 0x20U },  /* s */
    { 0x04U, 0x3FU, 0x44U, 0x40U, 0x20U },  /* t */
    { 0x3CU, 0x40U, 0x40U, 0x20U, 0x7CU },  /* u */
    { 0x1CU, 0x20U, 0x40U, 0x20U, 0x1CU },  /* v */
    { 0x3CU, 0x40U, 0x30U, 0x40U, 0x3CU },  /* w */
    { 0x44U, 0x28U, 0x10U, 0x28U, 0x44U },  /* x */
    { 0x0CU, 0x50U, 0x50U, 0x50U, 0x3CU },  /* y */
    { 0x44U, 0x64U, 0x54U, 0x4CU, 0x44U },  /* z */
    { 0x00U, 0x08U, 0x36U, 0x41U, 0x00U },  /* { */
    { 0x00U, 0x00U, 0x7FU, 0x00U, 0x00U },  /* | */
    { 0x00U, 0x41U, 0x36U, 0x08U, 0x00U },  /* } */
    { 0x08U, 0x08U, 0x2AU, 0x1CU, 0x08U }   /* ~ */
};

/* ==========================================================================
 *  ชุดคำสั่ง init
 *  SH1106 กับ SSD1306 ต่างกันแค่วิธีเปิดไฟเลี้ยงจอและค่า timing บางตัว
 * ========================================================================== */
static const uint8_t disp_init_cmds[] =
{
    0x00U,                    /* control byte: ที่เหลือเป็นคำสั่งทั้งหมด */
    0xAEU,                    /* display off */
    0xD5U, 0x80U,             /* clock divide / oscillator frequency */
    0xA8U, 0x3FU,             /* multiplex ratio = 64 แถว */
    0xD3U, 0x00U,             /* display offset = 0 */
    0x40U,                    /* start line = 0 */

#if (DISP_CONTROLLER == DISP_SH1106)
    0xADU, 0x8BU,             /* DC-DC ของ SH1106: เปิดเมื่อจอ on */
    0x33U,                    /* pump voltage 9.0V (คำสั่งเฉพาะ SH1106) */
#else
    0x8DU, 0x14U,             /* charge pump ของ SSD1306: เปิด */
    0x20U, 0x02U,             /* page addressing mode (เราตั้งหน้า/คอลัมน์เอง) */
#endif

#if (DISP_FLIP_180 != 0)
    0xA0U,                    /* segment remap ปกติ */
    0xC0U,                    /* COM scan ปกติ */
#else
    0xA1U,                    /* segment remap กลับด้าน */
    0xC8U,                    /* COM scan กลับด้าน */
#endif

    0xDAU, 0x12U,             /* COM pins: alternative, ไม่สลับซ้ายขวา */
    0x81U, DISP_CONTRAST_DEFAULT,

#if (DISP_CONTROLLER == DISP_SH1106)
    0xD9U, 0x22U,             /* pre-charge */
    0xDBU, 0x35U,             /* VCOMH deselect */
#else
    0xD9U, 0xF1U,
    0xDBU, 0x30U,
#endif

    0xA4U,                    /* แสดงตาม RAM ไม่ใช่ all-on */
    0xA6U,                    /* ภาพปกติ ไม่กลับสี */
    0xAFU                     /* display on */
};

/* ==========================================================================
 *  Private helpers — GPIO / I2C / DMA
 * ========================================================================== */

/**
 * @brief ตั้งขาเป็น alternate function open-drain พร้อม pull-up ภายใน
 * @note  pull-up ภายในของ STM32 อ่อน (~40k) ใช้เป็นตัวช่วยเท่านั้น
 *        ตัวจริงคือ pull-up บนโมดูลจอหรือบน shield
 */
static void Display_ConfigI2cPin(GPIO_TypeDef *port, uint32_t pin)
{
    port->MODER   &= ~(3UL << (pin * 2U));
    port->MODER   |=  (2UL << (pin * 2U));      /* alternate function */
    port->OTYPER  |=  (1UL << pin);             /* open drain */
    port->OSPEEDR |=  (3UL << (pin * 2U));      /* very high speed */
    port->PUPDR   &= ~(3UL << (pin * 2U));
    port->PUPDR   |=  (1UL << (pin * 2U));      /* pull-up */

    if (pin < 8U)
    {
        port->AFR[0] &= ~(0xFUL << (pin * 4U));
        port->AFR[0] |=  ((uint32_t)DISP_GPIO_AF << (pin * 4U));
    }
    else
    {
        port->AFR[1] &= ~(0xFUL << ((pin - 8U) * 4U));
        port->AFR[1] |=  ((uint32_t)DISP_GPIO_AF << ((pin - 8U) * 4U));
    }
}

/**
 * @brief ตั้งขาเป็น output open-drain ชั่วคราว ใช้ตอนกู้บัส
 */
static void Display_ConfigGpioOpenDrain(GPIO_TypeDef *port, uint32_t pin)
{
    port->BSRR     = (1UL << pin);              /* ปล่อยขาให้ลอยสูงก่อน */
    port->MODER   &= ~(3UL << (pin * 2U));
    port->MODER   |=  (1UL << (pin * 2U));      /* output */
    port->OTYPER  |=  (1UL << pin);
    port->PUPDR   &= ~(3UL << (pin * 2U));
    port->PUPDR   |=  (1UL << (pin * 2U));
}

static void Display_ShortDelay(void)
{
    volatile uint32_t i;
    for (i = 0U; i < 200U; i++)
    {
        /* busy wait สั้น ๆ ใช้เฉพาะตอน init และตอนกู้บัส */
    }
}

/**
 * @brief กู้บัส I2C ที่ค้างด้วยการ bit-bang SCL 9 ลูกแล้วสร้าง STOP เอง
 * @note  อาการที่ต้องใช้: กด reset MCU ตอนจอกำลังส่งข้อมูลอยู่ จอจะยัง
 *        กด SDA ค้างไว้รอ clock ที่เหลือ ทำให้ I2C ของ MCU มองว่าบัสไม่ว่าง
 *        และค้างตั้งแต่ START แรก — เจอบ่อยเวลา debug แล้วกด reset ถี่ ๆ
 */
static void Display_RecoverBus(void)
{
    uint32_t i;

    Display_ConfigGpioOpenDrain(DISP_SCL_PORT, DISP_SCL_PIN);
    Display_ConfigGpioOpenDrain(DISP_SDA_PORT, DISP_SDA_PIN);
    Display_ShortDelay();

    if ((DISP_SDA_PORT->IDR & (1UL << DISP_SDA_PIN)) == 0UL)
    {
        for (i = 0U; i < 9U; i++)
        {
            DISP_SCL_PORT->BSRR = (1UL << (DISP_SCL_PIN + 16U));
            Display_ShortDelay();
            DISP_SCL_PORT->BSRR = (1UL << DISP_SCL_PIN);
            Display_ShortDelay();
        }

        /* สร้าง STOP ด้วยมือ: SDA ขึ้นขณะ SCL สูง */
        DISP_SDA_PORT->BSRR = (1UL << (DISP_SDA_PIN + 16U));
        Display_ShortDelay();
        DISP_SCL_PORT->BSRR = (1UL << DISP_SCL_PIN);
        Display_ShortDelay();
        DISP_SDA_PORT->BSRR = (1UL << DISP_SDA_PIN);
        Display_ShortDelay();
    }
}

static void Display_ConfigI2c(void)
{
    /* reset peripheral ให้กลับสู่สถานะรู้จัก เผื่อค้างจากรอบก่อน */
    DISP_I2C->CR1 = I2C_CR1_SWRST;
    DISP_I2C->CR1 = 0UL;

    DISP_I2C->CR2   = (uint32_t)DISP_PCLK1_MHZ;   /* บอกความถี่ APB1 เป็น MHz */
    DISP_I2C->CCR   = (uint32_t)DISP_I2C_CCR;
    DISP_I2C->TRISE = (uint32_t)DISP_I2C_TRISE;
    DISP_I2C->CR1  |= I2C_CR1_PE;
}

static void Display_ConfigDma(void)
{
    /* ปิด stream ก่อนตั้งค่า — ตอน init ยังไม่มีใครใช้ จึงไม่ต้องรอ */
    DISP_DMA_STREAM->CR = 0UL;
    DISP_DMA_CLEAR_FLAGS();

    /* MISRA C:2012 Rule 11.4 deviation: DMA peripheral address register
     * รับค่าเป็น uint32_t ตามฮาร์ดแวร์ เลี่ยงการ cast pointer ไม่ได้ */
    DISP_DMA_STREAM->PAR  = (uint32_t)&DISP_I2C->DR;
    DISP_DMA_STREAM->FCR  = 0UL;                       /* direct mode ไม่ใช้ FIFO */
    DISP_DMA_STREAM->CR   = (DISP_DMA_CHANNEL << DMA_SxCR_CHSEL_Pos)
                          | (1UL << DMA_SxCR_DIR_Pos)  /* memory -> peripheral */
                          | DMA_SxCR_MINC              /* เดินหน้าเฉพาะฝั่ง memory */
                          | (2UL << DMA_SxCR_PL_Pos)   /* priority high */
                          | DMA_SxCR_TCIE
                          | DMA_SxCR_TEIE;
}

/**
 * @brief เขียนข้อมูลชุดหนึ่งแบบ block พร้อม timeout
 * @note  ใช้เฉพาะตอน init และ Display_FlushBlocking() เท่านั้น
 *        ห้ามเรียกจาก main loop ของเกม และห้ามเรียกขณะ DMA กำลังทำงาน
 */
static bool Display_WriteBlocking(const uint8_t *data, uint16_t length)
{
    uint32_t guard;
    uint16_t index = 0U;
    bool     ok    = true;

    /* รอบัสว่าง */
    guard = 0U;
    while (((DISP_I2C->SR2 & I2C_SR2_BUSY) != 0UL) && (guard < DISP_BLOCK_TIMEOUT))
    {
        guard++;
    }
    if (guard >= DISP_BLOCK_TIMEOUT)
    {
        ok = false;
    }

    if (ok)
    {
        DISP_I2C->CR1 |= I2C_CR1_START;
        guard = 0U;
        while (((DISP_I2C->SR1 & I2C_SR1_SB) == 0UL) && (guard < DISP_BLOCK_TIMEOUT))
        {
            guard++;
        }
        ok = (guard < DISP_BLOCK_TIMEOUT);
    }

    if (ok)
    {
        DISP_I2C->DR = (uint32_t)((uint32_t)DISP_ADDR << 1U);   /* บิต 0 = 0 คือเขียน */

        guard = 0U;
        while (((DISP_I2C->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF)) == 0UL) &&
               (guard < DISP_BLOCK_TIMEOUT))
        {
            guard++;
        }

        if ((DISP_I2C->SR1 & I2C_SR1_AF) != 0UL)
        {
            DISP_I2C->SR1 &= ~I2C_SR1_AF;   /* จอไม่ตอบ ACK */
            ok = false;
        }
        else if (guard >= DISP_BLOCK_TIMEOUT)
        {
            ok = false;
        }
        else
        {
            (void)DISP_I2C->SR2;            /* อ่าน SR1 แล้ว SR2 เพื่อเคลียร์ ADDR */
        }
    }

    while (ok && (index < length))
    {
        guard = 0U;
        while (((DISP_I2C->SR1 & I2C_SR1_TXE) == 0UL) && (guard < DISP_BLOCK_TIMEOUT))
        {
            guard++;
        }

        if (guard >= DISP_BLOCK_TIMEOUT)
        {
            ok = false;
        }
        else
        {
            DISP_I2C->DR = (uint32_t)data[index];
            index++;
        }
    }

    if (ok)
    {
        guard = 0U;
        while (((DISP_I2C->SR1 & I2C_SR1_BTF) == 0UL) && (guard < DISP_BLOCK_TIMEOUT))
        {
            guard++;
        }
        ok = (guard < DISP_BLOCK_TIMEOUT);
    }

    /* STOP ทุกกรณี รวมถึงตอนล้มเหลว เพื่อไม่ให้บัสค้าง */
    DISP_I2C->CR1 |= I2C_CR1_STOP;

    if (!ok)
    {
        if (disp_errors < UINT16_MAX)
        {
            disp_errors++;
        }
    }

    return ok;
}

/**
 * @brief ยกเลิกการส่งที่ค้างอยู่ แล้วปล่อยบัส
 * @note  เรียกได้จากทั้ง ISR และ main loop
 *        ฝั่ง main loop ต้องปิด interrupt ครอบไว้เอง
 */
static void Display_Abort(void)
{
    DISP_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    DISP_DMA_CLEAR_FLAGS();

    DISP_I2C->CR2 &= ~(I2C_CR2_DMAEN | I2C_CR2_ITEVTEN | I2C_CR2_ITERREN);
    DISP_I2C->CR1 |= I2C_CR1_STOP;

    disp_busy = false;

    if (disp_errors < UINT16_MAX)
    {
        disp_errors++;
    }
}

/**
 * @brief ตั้ง DMA แล้วสั่ง START ให้หน้าที่ระบุ
 * @note  เรียกจาก Display_Flush() (หน้าแรก) และจาก ISR (หน้าถัดไป)
 *        ไม่มี loop รอ EN เพราะจุดที่เรียกทั้งสองแห่งการันตีว่า stream ว่างแล้ว
 *        (หน้าแรกเช็คจาก disp_busy, หน้าถัดไปเข้ามาหลัง transfer complete)
 */
static void Display_StartPage(uint8_t page)
{
    DISP_DMA_CLEAR_FLAGS();
    /* MISRA C:2012 Rule 11.4 deviation: เหตุผลเดียวกับ PAR ใน Display_ConfigDma() */
    DISP_DMA_STREAM->M0AR = (uint32_t)(&disp_fb[page][0]);
    DISP_DMA_STREAM->NDTR = (uint32_t)DISP_PAGE_TX_LEN;
    DISP_DMA_STREAM->CR  |= DMA_SxCR_EN;

    /* ห้ามเปิด DMAEN ตรงนี้เด็ดขาด
     * ตอนขึ้นหน้าใหม่ด้วย repeated start สถานะบัสคือ BTF=1 และ TXE=1 อยู่แล้ว
     * ถ้าเปิด DMAEN ก่อน DMA จะยิงไบต์แรกลง DR ทันทีตั้งแต่ยังไม่ได้ส่ง address
     * ไบต์นั้นจะกลายเป็นข้อมูลของ transaction เก่า แล้วภาพจะเลื่อนทั้งเฟรม
     * จึงต้องไปเปิดใน ISR หลังเคลียร์ ADDR แทน */
    DISP_I2C->CR2 |= (I2C_CR2_ITEVTEN | I2C_CR2_ITERREN);
    DISP_I2C->CR1 |= I2C_CR1_START;   /* หน้าที่ 2 ขึ้นไปกลายเป็น repeated start */
}

/**
 * @brief ปิดหน้าปัจจุบัน แล้วไปหน้าถัดไปหรือจบเฟรม
 * @note  เรียกจาก I2C event ISR ตอน BTF เท่านั้น
 */
static void Display_PageDone(void)
{
    uint8_t next = (uint8_t)(disp_page + 1U);

    if (next < (uint8_t)DISPLAY_PAGES)
    {
        disp_page = next;
        Display_StartPage(next);
    }
    else
    {
        DISP_I2C->CR1 |= I2C_CR1_STOP;
        DISP_I2C->CR2 &= ~I2C_CR2_ITERREN;
        disp_busy = false;
    }
}

/* ==========================================================================
 *  Private helpers — framebuffer
 * ========================================================================== */

/**
 * @brief นำ mask ไปกระทำกับหนึ่งคอลัมน์ของหนึ่งหน้า
 * @note  page และ x ต้องอยู่ในขอบเขตแล้วก่อนเรียก ผู้เรียกเป็นคน clip
 */
static void Display_ApplyMask(uint32_t page, uint32_t x, uint8_t mask,
                              Display_Color_t color)
{
    uint8_t *cell = &disp_fb[page][DISP_PAGE_HEADER + x];

    if (color == DISPLAY_WHITE)
    {
        *cell |= mask;
    }
    else if (color == DISPLAY_BLACK)
    {
        *cell &= (uint8_t)(~mask);
    }
    else
    {
        *cell ^= mask;
    }
}

/**
 * @brief ตัดสี่เหลี่ยมให้อยู่ในจอ คืน false ถ้าตกนอกจอทั้งหมด
 */
static bool Display_ClipRect(int16_t x, int16_t y, int16_t width, int16_t height,
                             int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t left   = (int32_t)x;
    int32_t top    = (int32_t)y;
    int32_t right  = left + (int32_t)width  - 1;
    int32_t bottom = top  + (int32_t)height - 1;
    bool    visible;

    if (left < 0)
    {
        left = 0;
    }
    if (top < 0)
    {
        top = 0;
    }
    if (right > ((int32_t)DISPLAY_WIDTH - 1))
    {
        right = (int32_t)DISPLAY_WIDTH - 1;
    }
    if (bottom > ((int32_t)DISPLAY_HEIGHT - 1))
    {
        bottom = (int32_t)DISPLAY_HEIGHT - 1;
    }

    visible = ((width > 0) && (height > 0) && (left <= right) && (top <= bottom));

    if (visible)
    {
        *x0 = left;
        *y0 = top;
        *x1 = right;
        *y1 = bottom;
    }

    return visible;
}

/* ==========================================================================
 *  Public API — setup
 * ========================================================================== */

bool Display_Init(void)
{
    uint32_t page;
    uint32_t column;

    /* 1) เปิด clock ทั้งสามตัวที่ต้องใช้
     *    ต้องอ่านค่ากลับทันที เพราะบน F4 คำสั่งเขียนถัดไปที่วิ่งไปหา peripheral
     *    อาจมาถึงก่อน clock จะเปิดจริง แล้วหายเงียบ ๆ (RM0383 หัวข้อ RCC)
     *    บรรทัดถัดจากนี้คือ Display_RecoverBus() ที่เขียน GPIO ทันที จึงเสี่ยงตรง ๆ */
    RCC->AHB1ENR |= (DISP_GPIO_CLK_EN | DISP_DMA_CLK_EN);
    (void)RCC->AHB1ENR;
    RCC->APB1ENR |= DISP_I2C_CLK_EN;
    (void)RCC->APB1ENR;

    /* 2) กู้บัสก่อน แล้วค่อยมอบขาให้ peripheral */
    Display_RecoverBus();
    Display_ConfigI2cPin(DISP_SCL_PORT, DISP_SCL_PIN);
    Display_ConfigI2cPin(DISP_SDA_PORT, DISP_SDA_PIN);

    /* 3) ตั้ง I2C และ DMA */
    Display_ConfigI2c();
    Display_ConfigDma();

    /* 4) เติม header ประจำหน้าให้ buffer ครั้งเดียวตลอดอายุโปรแกรม
     *    ตั้งแต่นี้ไปโค้ดวาดแตะเฉพาะช่วง pixel ไม่แตะ header อีกเลย */
    for (page = 0U; page < DISPLAY_PAGES; page++)
    {
        disp_fb[page][0] = 0x80U;
        disp_fb[page][1] = (uint8_t)(0xB0U | page);
        disp_fb[page][2] = 0x80U;
        disp_fb[page][3] = (uint8_t)(0x00U | (DISP_COL_OFFSET & 0x0FU));
        disp_fb[page][4] = 0x80U;
        disp_fb[page][5] = (uint8_t)(0x10U | (DISP_COL_OFFSET >> 4U));
        disp_fb[page][6] = 0x40U;

        for (column = 0U; column < DISPLAY_WIDTH; column++)
        {
            disp_fb[page][DISP_PAGE_HEADER + column] = 0x00U;
        }
    }

    /* 5) ส่งชุด init แบบ block — ถ้าจอไม่ตอบ ACK ก็รู้ตั้งแต่ตรงนี้ */
    disp_present = Display_WriteBlocking(disp_init_cmds, (uint16_t)sizeof(disp_init_cmds));

    if (disp_present)
    {
        /* 6) ล้างจอจริงก่อน เพื่อไม่ให้ขยะใน RAM ของจอโผล่ตอนเปิดเครื่อง */
        (void)Display_FlushBlocking();

        /* 7) เปิด interrupt ให้เส้นทาง DMA ทำงานตั้งแต่เฟรมถัดไป */
        NVIC_SetPriority(DISP_DMA_IRQn, DISP_IRQ_PRIORITY);
        NVIC_SetPriority(DISP_I2C_EV_IRQn, DISP_IRQ_PRIORITY);
        NVIC_SetPriority(DISP_I2C_ER_IRQn, DISP_IRQ_PRIORITY);
        NVIC_EnableIRQ(DISP_DMA_IRQn);
        NVIC_EnableIRQ(DISP_I2C_EV_IRQn);
        NVIC_EnableIRQ(DISP_I2C_ER_IRQn);
    }

    disp_busy   = false;
    disp_page   = 0U;
    disp_stall  = 0U;
    disp_errors = 0U;

    return disp_present;
}

bool Display_IsPresent(void)
{
    return disp_present;
}

void Display_SetContrast(uint8_t contrast)
{
    uint8_t command[3];

    command[0] = 0x00U;
    command[1] = 0x81U;
    command[2] = contrast;

    if (disp_present && (!disp_busy))
    {
        (void)Display_WriteBlocking(command, 3U);
    }
}

/* ==========================================================================
 *  Public API — การวาด
 * ========================================================================== */

void Display_Clear(void)
{
    Display_Fill(DISPLAY_BLACK);
}

void Display_Fill(Display_Color_t color)
{
    uint32_t page;
    uint32_t column;
    uint8_t  value = (color == DISPLAY_WHITE) ? 0xFFU : 0x00U;

    if (color == DISPLAY_INVERT)
    {
        for (page = 0U; page < DISPLAY_PAGES; page++)
        {
            for (column = 0U; column < DISPLAY_WIDTH; column++)
            {
                disp_fb[page][DISP_PAGE_HEADER + column] ^= 0xFFU;
            }
        }
    }
    else
    {
        for (page = 0U; page < DISPLAY_PAGES; page++)
        {
            for (column = 0U; column < DISPLAY_WIDTH; column++)
            {
                disp_fb[page][DISP_PAGE_HEADER + column] = value;
            }
        }
    }
}

void Display_DrawPixel(int16_t x, int16_t y, Display_Color_t color)
{
    if ((x >= 0) && (x < (int16_t)DISPLAY_WIDTH) &&
        (y >= 0) && (y < (int16_t)DISPLAY_HEIGHT))
    {
        uint32_t page = (uint32_t)y >> 3U;
        uint8_t  mask = (uint8_t)(1U << ((uint32_t)y & 7U));

        Display_ApplyMask(page, (uint32_t)x, mask, color);
    }
}

void Display_FillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                      Display_Color_t color)
{
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (Display_ClipRect(x, y, width, height, &x0, &y0, &x1, &y1))
    {
        uint32_t first_page = (uint32_t)y0 >> 3U;
        uint32_t last_page  = (uint32_t)y1 >> 3U;
        uint32_t page;

        /* ไล่ทีละหน้าแล้วเขียนทั้งไบต์ เร็วกว่าไล่ทีละ pixel ประมาณ 8 เท่า
         * ซึ่งสำคัญเพราะเกมวาดสี่เหลี่ยมใหม่ทุกเฟรม */
        for (page = first_page; page <= last_page; page++)
        {
            int32_t page_top    = (int32_t)(page * 8U);
            int32_t page_bottom = page_top + 7;
            int32_t row_start   = (y0 > page_top)    ? (y0 - page_top) : 0;
            int32_t row_end     = (y1 < page_bottom) ? (y1 - page_top) : 7;
            uint32_t bit_count  = (uint32_t)((row_end - row_start) + 1);
            uint8_t  mask       = (uint8_t)(((1U << bit_count) - 1U) << (uint32_t)row_start);
            int32_t  column;

            for (column = x0; column <= x1; column++)
            {
                Display_ApplyMask(page, (uint32_t)column, mask, color);
            }
        }
    }
}

void Display_DrawHLine(int16_t x, int16_t y, int16_t width, Display_Color_t color)
{
    Display_FillRect(x, y, width, 1, color);
}

void Display_DrawVLine(int16_t x, int16_t y, int16_t height, Display_Color_t color)
{
    Display_FillRect(x, y, 1, height, color);
}

void Display_DrawRect(int16_t x, int16_t y, int16_t width, int16_t height,
                      Display_Color_t color)
{
    if ((width > 0) && (height > 0))
    {
        Display_DrawHLine(x, y, width, color);
        Display_DrawHLine(x, (int16_t)((int32_t)y + (int32_t)height - 1), width, color);
        Display_DrawVLine(x, y, height, color);
        Display_DrawVLine((int16_t)((int32_t)x + (int32_t)width - 1), y, height, color);
    }
}

void Display_DrawBitmap(int16_t x, int16_t y, uint8_t width, uint8_t height,
                        const uint8_t *bitmap, Display_Color_t color)
{
    if ((bitmap != NULL) && (width > 0U) && (height > 0U))
    {
        uint32_t bytes_per_row = ((uint32_t)width + 7U) / 8U;
        uint32_t row;
        uint32_t column;

        for (row = 0U; row < (uint32_t)height; row++)
        {
            for (column = 0U; column < (uint32_t)width; column++)
            {
                uint8_t byte = bitmap[(row * bytes_per_row) + (column >> 3U)];
                uint8_t bit  = (uint8_t)(0x80U >> (column & 7U));

                if ((byte & bit) != 0U)
                {
                    Display_DrawPixel((int16_t)((int32_t)x + (int32_t)column),
                                      (int16_t)((int32_t)y + (int32_t)row),
                                      color);
                }
                /* บิต 0 ปล่อยผ่าน ไม่ลบพื้นหลัง sprite จึงโปร่งใส */
            }
        }
    }
}

void Display_DrawChar(int16_t x, int16_t y, char character, Display_Color_t color)
{
    uint8_t  code = (uint8_t)character;
    uint32_t index;
    uint32_t column;
    uint32_t row;

    if ((code < DISP_FONT_FIRST) || (code > DISP_FONT_LAST))
    {
        code = (uint8_t)'?';
    }
    index = (uint32_t)code - DISP_FONT_FIRST;

    for (column = 0U; column < DISP_FONT_GLYPH_WIDTH; column++)
    {
        uint8_t bits = disp_font[index][column];

        for (row = 0U; row < DISP_FONT_GLYPH_HEIGHT; row++)
        {
            if ((bits & (uint8_t)(1U << row)) != 0U)
            {
                Display_DrawPixel((int16_t)((int32_t)x + (int32_t)column),
                                  (int16_t)((int32_t)y + (int32_t)row),
                                  color);
            }
        }
    }
}

void Display_DrawText(int16_t x, int16_t y, const char *text, Display_Color_t color)
{
    if (text != NULL)
    {
        int32_t     cursor = (int32_t)x;
        const char *cursor_text = text;

        while (*cursor_text != '\0')
        {
            /* ข้ามตัวที่ยังไม่ถึงจอ และหยุดเมื่อเลยขอบขวาไปแล้ว */
            if (cursor >= (int32_t)DISPLAY_WIDTH)
            {
                break;
            }
            if (cursor > -(int32_t)DISPLAY_FONT_WIDTH)
            {
                Display_DrawChar((int16_t)cursor, y, *cursor_text, color);
            }

            cursor += (int32_t)DISPLAY_FONT_WIDTH;
            cursor_text++;
        }
    }
}

void Display_DrawNumber(int16_t x, int16_t y, uint32_t value, Display_Color_t color)
{
    uint8_t  digits[10];              /* uint32_t สูงสุด 10 หลัก ไม่ต้องมีตัวปิดสตริง */
    uint32_t count  = 0U;
    uint32_t rest   = value;
    int32_t  cursor = (int32_t)x;     /* Rule 17.8: ห้ามแก้ค่า parameter x โดยตรง */

    do
    {
        /* Rule 10.1/10.3: คิดเลขบน uint8_t แล้วค่อย cast ตอนวาด
         * ไม่คิดเลขบน char ซึ่ง essential type ไม่ใช่ตัวเลข */
        digits[count] = (uint8_t)((uint8_t)'0' + (uint8_t)(rest % 10U));
        rest /= 10U;
        count++;
    } while ((rest != 0U) && (count < 10U));

    /* สร้างจากหลักท้ายไปหน้า จึงต้องวาดย้อนกลับ */
    while (count > 0U)
    {
        count--;
        Display_DrawChar((int16_t)cursor, y, (char)digits[count], color);
        cursor += (int32_t)DISPLAY_FONT_WIDTH;
    }
}

uint16_t Display_TextWidth(const char *text)
{
    uint16_t width = 0U;

    if (text != NULL)
    {
        const char *cursor = text;
        uint16_t    count  = 0U;

        while ((*cursor != '\0') && (count < 255U))
        {
            count++;
            cursor++;
        }

        if (count > 0U)
        {
            /* ไม่นับช่องว่างหลังตัวสุดท้าย เพื่อให้จัดกลางจอแล้วตรงจริง */
            width = (uint16_t)((count * DISPLAY_FONT_WIDTH) - 1U);
        }
    }

    return width;
}

/* ==========================================================================
 *  Public API — การส่งภาพออกจอ
 * ========================================================================== */

bool Display_Flush(void)
{
    bool started = false;

    if (!disp_present)
    {
        /* ไม่มีจอ: คืน false เงียบ ๆ เกมยังเดินต่อได้ตามปกติ */
    }
    else if (disp_busy)
    {
        /* ค้างนานผิดปกติ = จอหลุดกลางคันหรือบัสมีปัญหา ยกเลิกรอบนั้นทิ้ง
         * ไม่งั้น disp_busy จะค้าง true ตลอดกาลและภาพหยุดนิ่งถาวร */
        if (disp_stall < UINT8_MAX)
        {
            disp_stall++;
        }
        if (disp_stall >= DISP_STALL_LIMIT)
        {
            /* เก็บ PRIMASK เดิมไว้ก่อน ไม่ใช้ __enable_irq() ตรง ๆ
             * เพราะถ้าวันหลังมีคนเรียก Display_Flush() จากบริบทที่ปิด
             * interrupt ไว้อยู่แล้ว บรรทัดนั้นจะเปิดคืนโดยไม่ตั้งใจ */
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            Display_Abort();
            __set_PRIMASK(primask);
            disp_stall = 0U;
        }
    }
    else if ((DISP_I2C->SR2 & I2C_SR2_BUSY) != 0UL)
    {
        /* บัสยังไม่ว่าง — STOP ของรอบก่อน (หรือของ Display_Abort) ยังไม่จบ
         * ถ้าสั่ง START ทับตรงนี้ SB จะไม่มา แล้ว disp_busy ค้างยาว
         * ข้ามเฟรมนี้ไปเฉย ๆ ไม่วนรอ */
    }
    else
    {
        disp_stall = 0U;
        disp_page  = 0U;
        disp_busy  = true;
        Display_StartPage(0U);
        started = true;
    }

    return started;
}

bool Display_IsBusy(void)
{
    return disp_busy;
}

bool Display_FlushBlocking(void)
{
    uint32_t page;
    bool     ok = disp_present || (disp_errors == 0U);   /* ตอน init ยังไม่รู้ผล */

    if (!disp_busy)
    {
        for (page = 0U; (page < DISPLAY_PAGES) && ok; page++)
        {
            ok = Display_WriteBlocking(&disp_fb[page][0], (uint16_t)DISP_PAGE_TX_LEN);
        }
    }
    else
    {
        ok = false;   /* DMA กำลังใช้ buffer อยู่ ห้ามแทรก */
    }

    return ok;
}

uint16_t Display_GetErrorCount(void)
{
    return disp_errors;
}

/* ==========================================================================
 *  Interrupt context — สั้นที่สุด ไม่มี loop ไม่มีการเรียก game logic
 * ========================================================================== */

void Display_DmaIrqHandler(void)
{
    uint32_t status = DISP_DMA_ISR();

    if ((status & DISP_DMA_TEIF) != 0UL)
    {
        DISP_DMA_CLEAR_FLAGS();
        Display_Abort();
    }
    else if ((status & DISP_DMA_TCIF) != 0UL)
    {
        DISP_DMA_CLEAR_FLAGS();
        DISP_DMA_STREAM->CR &= ~DMA_SxCR_EN;

        /* ข้อมูลหมดฝั่ง DMA แล้ว แต่ไบต์สุดท้ายอาจยังอยู่ใน shift register
         * จึงต้องปิด DMA แล้วรอ BTF ก่อนถึงจะ STOP หรือขึ้นหน้าใหม่ได้ */
        DISP_I2C->CR2 &= ~I2C_CR2_DMAEN;
        DISP_I2C->CR2 |= I2C_CR2_ITEVTEN;
    }
    else
    {
        /* flag อื่นไม่สนใจ */
    }
}

void Display_I2cEventIrqHandler(void)
{
    uint32_t status = DISP_I2C->SR1;

    if ((status & I2C_SR1_SB) != 0UL)
    {
        /* อ่าน SR1 ไปแล้วด้านบน การเขียน DR ต่อจากนี้จะเคลียร์ SB เอง */
        DISP_I2C->DR = (uint32_t)((uint32_t)DISP_ADDR << 1U);
    }
    else if ((status & I2C_SR1_ADDR) != 0UL)
    {
        (void)DISP_I2C->SR2;                 /* SR1 แล้ว SR2 = เคลียร์ ADDR */

        /* เปิด DMA ตรงนี้เท่านั้น: address phase จบแล้ว TXE ที่ตามมาจึงเป็น
         * request ของข้อมูลจริง ไม่ไปโผล่กลาง transaction ก่อนหน้า */
        DISP_I2C->CR2 |= I2C_CR2_DMAEN;
        DISP_I2C->CR2 &= ~I2C_CR2_ITEVTEN;   /* จากนี้ DMA รับช่วงต่อ รอ BTF ทีหลัง */
    }
    else if ((status & I2C_SR1_BTF) != 0UL)
    {
        DISP_I2C->CR2 &= ~I2C_CR2_ITEVTEN;
        Display_PageDone();
    }
    else
    {
        /* event อื่นไม่ได้เปิดไว้ */
    }
}

void Display_I2cErrorIrqHandler(void)
{
    uint32_t status = DISP_I2C->SR1;

    if ((status & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR)) != 0UL)
    {
        DISP_I2C->SR1 &= ~(I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR);
        Display_Abort();
    }
}
