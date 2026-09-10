/**
 ******************************************************************************
 * @file    main.c
 * @brief   STM32 Mini Racing Game — bring-up ขั้นที่ 1: ทดสอบ joystick
 *          Nucleo-F411RE + STEO Training Shield 1
 *
 * ขั้นนี้ยังไม่เรียก game logic เพราะ button driver และ display ยังว่าง
 * เป้าหมายคือพิสูจน์ว่า ADC interrupt ทำงานและทิศทางถูกต้อง
 *
 *   โยกซ้าย  -> ไฟแดง   D12 (PA6) ติด
 *   โยกขวา   -> ไฟเหลือง D11 (PA7) ติด
 *   ตรงกลาง  -> ดับทั้งคู่
 *
 * ถ้าไฟติดสลับข้าง ให้ตั้ง JOY_INVERT_X เป็น 1 ใน joystick.c
 ******************************************************************************
 */

#include "stm32f411xe.h"
#include "joystick.h"

/* LED บน STEO shield */
#define LED_LEFT_PIN    6U      /* D12 / PA6 สีแดง */
#define LED_RIGHT_PIN   7U      /* D11 / PA7 สีเหลือง */

/* หนึ่ง tick = 20 ms ตามที่ game logic กำหนดไว้ */
#define TICK_HZ         50UL
#define SYS_CLOCK_HZ    84000000UL

/**
 * @brief จำนวน tick ที่ยังไม่ได้ประมวลผล
 * @note  SysTick_Handler เพิ่มค่า, main loop ลดค่า
 *        ใช้ counter ไม่ใช่ bool เพื่อไม่ให้ tick หายเวลา main loop ช้า
 */
volatile uint32_t tick_pending = 0U;

static void SystemClock_Config(void);
static void LED_Init(void);

int main(void)
{
    Joystick_Dir_t dir;

    SystemClock_Config();
    LED_Init();

    Joystick_Init();
    Joystick_Calibrate();          /* ห้ามแตะจอยตอนนี้ */

    SysTick_Config(SYS_CLOCK_HZ / TICK_HZ);

    while (1)
    {
        if (tick_pending > 0U)
        {
            /* ลดค่าแบบ atomic: ปิด interrupt ชั่วขณะ กัน SysTick แทรกกลางคัน */
            __disable_irq();
            tick_pending--;
            __enable_irq();

            Joystick_StartSample();          /* ไม่ block ผลมาทาง ADC interrupt */
            dir = Joystick_GetDirection();

            switch (dir)
            {
                case JOYSTICK_LEFT:
                    GPIOA->BSRR = (1UL << LED_LEFT_PIN);
                    GPIOA->BSRR = (1UL << (LED_RIGHT_PIN + 16U));
                    break;

                case JOYSTICK_RIGHT:
                    GPIOA->BSRR = (1UL << (LED_LEFT_PIN + 16U));
                    GPIOA->BSRR = (1UL << LED_RIGHT_PIN);
                    break;

                default:
                    GPIOA->BSRR = (1UL << (LED_LEFT_PIN + 16U));
                    GPIOA->BSRR = (1UL << (LED_RIGHT_PIN + 16U));
                    break;
            }

            /* ขั้นถัดไปเมื่อ button driver พร้อม:
             *
             *   GameInput_t in;
             *   in.direction      = map_dir(dir);
             *   in.button_pressed = Button_TakePressEvent();
             *   GameEvents_t ev   = Game_Update(&game, in);
             */
        }
    }
}

/**
 * @brief HSI 16 MHz -> PLL -> SYSCLK 84 MHz, APB1 42 MHz, APB2 84 MHz
 * @note  ถ้าเปลี่ยนความถี่ ต้องกลับไปดู ADC prescaler ใน joystick.c ด้วย
 */
static void SystemClock_Config(void)
{
    /* Flash: 2 wait states สำหรับ 84 MHz ที่ 3.3V + เปิด cache/prefetch */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN
               | FLASH_ACR_LATENCY_2WS;

    /* Voltage scale 1 */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR      |= PWR_CR_VOS;

    /* HSI เปิดอยู่แล้วหลัง reset แต่ยืนยันอีกรอบ */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0UL)
    {
    }

    /* ปิด PLL ก่อนตั้งค่า */
    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0UL)
    {
    }

    /* 16 MHz / M16 = 1 MHz, x N336 = 336 MHz, / P4 = 84 MHz */
    RCC->PLLCFGR = (16UL  << RCC_PLLCFGR_PLLM_Pos)
                 | (336UL << RCC_PLLCFGR_PLLN_Pos)
                 | (1UL   << RCC_PLLCFGR_PLLP_Pos)   /* 01 = หาร 4 */
                 | (7UL   << RCC_PLLCFGR_PLLQ_Pos);

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0UL)
    {
    }

    /* AHB /1, APB1 /2 (42 MHz), APB2 /1 (84 MHz) */
    RCC->CFGR = RCC_CFGR_PPRE1_DIV2;

    /* สลับมาใช้ PLL */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
    }

    SystemCoreClock = SYS_CLOCK_HZ;
}

static void LED_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    /* PA6, PA7 เป็น output push-pull ไม่มี pull */
    GPIOA->MODER &= ~((3UL << (LED_LEFT_PIN * 2U)) | (3UL << (LED_RIGHT_PIN * 2U)));
    GPIOA->MODER |=  ((1UL << (LED_LEFT_PIN * 2U)) | (1UL << (LED_RIGHT_PIN * 2U)));

    GPIOA->OTYPER &= ~((1UL << LED_LEFT_PIN) | (1UL << LED_RIGHT_PIN));
    GPIOA->PUPDR  &= ~((3UL << (LED_LEFT_PIN * 2U)) | (3UL << (LED_RIGHT_PIN * 2U)));

    /* เริ่มต้นดับทั้งคู่ */
    GPIOA->BSRR = (1UL << (LED_LEFT_PIN + 16U)) | (1UL << (LED_RIGHT_PIN + 16U));
}
