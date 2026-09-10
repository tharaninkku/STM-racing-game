/**
 ******************************************************************************
 * @file      startup_stm32f411retx.s
 * @brief     STM32F411RETx startup code + interrupt vector table
 *
 * หน้าที่:
 *   - ตั้ง stack pointer
 *   - เรียก SystemInit()
 *   - copy .data จาก flash มา RAM และเคลียร์ .bss
 *   - เรียก __libc_init_array() แล้วเข้า main()
 *   - ประกาศตาราง vector ทั้งหมดเป็น weak alias ของ Default_Handler
 *
 * handler ตัวไหนที่เขียนไว้ใน stm32f4xx_it.c จะไปทับ weak symbol เอง
 * ตัวที่ไม่ได้เขียนจะวนอยู่ใน Default_Handler
 ******************************************************************************
 */

  .syntax unified
  .cpu cortex-m4
  .fpu softvfp
  .thumb

.global  g_pfnVectors
.global  Default_Handler

/* สัญลักษณ์ที่ linker script กำหนดให้ */
.word  _sidata      /* ตำแหน่งค่าเริ่มต้นของ .data ใน flash */
.word  _sdata       /* ต้น .data ใน RAM */
.word  _edata       /* ท้าย .data ใน RAM */
.word  _sbss        /* ต้น .bss */
.word  _ebss        /* ท้าย .bss */

/**
 * @brief  จุดเริ่มต้นหลัง reset
 */
  .section  .text.Reset_Handler
  .weak  Reset_Handler
  .type  Reset_Handler, %function
Reset_Handler:
  ldr   r0, =_estack
  mov   sp, r0

/* เรียก SystemInit ก่อน เพราะต้องเปิด FPU ก่อนโค้ด C ที่ใช้ float ทำงาน */
  bl  SystemInit

/* copy .data จาก flash มา RAM */
  ldr r0, =_sdata
  ldr r1, =_edata
  ldr r2, =_sidata
  movs r3, #0
  b LoopCopyDataInit

CopyDataInit:
  ldr r4, [r2, r3]
  str r4, [r0, r3]
  adds r3, r3, #4

LoopCopyDataInit:
  adds r4, r0, r3
  cmp r4, r1
  bcc CopyDataInit

/* เคลียร์ .bss เป็นศูนย์ */
  ldr r2, =_sbss
  ldr r4, =_ebss
  movs r3, #0
  b LoopFillZerobss

FillZerobss:
  str  r3, [r2]
  adds r2, r2, #4

LoopFillZerobss:
  cmp r2, r4
  bcc FillZerobss

/* เรียก constructor ของ C++/static แล้วเข้า main */
  bl  __libc_init_array
  bl  main

/* ถ้า main return (ไม่ควรเกิด) ให้ค้างไว้ */
LoopForever:
  b LoopForever

.size  Reset_Handler, .-Reset_Handler

/**
 * @brief  handler สำรองสำหรับ interrupt ที่ยังไม่ได้เขียน
 * @note   ถ้าโปรแกรมมาค้างที่นี่ แปลว่ามี interrupt ถูกเปิดไว้แต่ไม่มีคนรับ
 *         ตั้ง breakpoint ตรงนี้เวลา debug จะจับได้ทันที
 */
  .section  .text.Default_Handler,"ax",%progbits
Default_Handler:
InfiniteLoop:
  b  InfiniteLoop
  .size  Default_Handler, .-Default_Handler

/******************************************************************************
 * ตาราง vector ของ STM32F411RETx
 * ตำแหน่งต้องตรงตาม reference manual เป๊ะ ๆ ห้ามสลับหรือลบช่องว่าง
 ******************************************************************************/
  .section  .isr_vector,"a",%progbits
  .type  g_pfnVectors, %object

g_pfnVectors:
  .word  _estack
  .word  Reset_Handler
  .word  NMI_Handler
  .word  HardFault_Handler
  .word  MemManage_Handler
  .word  BusFault_Handler
  .word  UsageFault_Handler
  .word  0
  .word  0
  .word  0
  .word  0
  .word  SVC_Handler
  .word  DebugMon_Handler
  .word  0
  .word  PendSV_Handler
  .word  SysTick_Handler

  /* --- external interrupts เริ่มที่ IRQ 0 --- */
  .word  WWDG_IRQHandler                   /* 0  */
  .word  PVD_IRQHandler                    /* 1  */
  .word  TAMP_STAMP_IRQHandler             /* 2  */
  .word  RTC_WKUP_IRQHandler               /* 3  */
  .word  FLASH_IRQHandler                  /* 4  */
  .word  RCC_IRQHandler                    /* 5  */
  .word  EXTI0_IRQHandler                  /* 6  */
  .word  EXTI1_IRQHandler                  /* 7  */
  .word  EXTI2_IRQHandler                  /* 8  */
  .word  EXTI3_IRQHandler                  /* 9  */
  .word  EXTI4_IRQHandler                  /* 10 */
  .word  DMA1_Stream0_IRQHandler           /* 11 */
  .word  DMA1_Stream1_IRQHandler           /* 12 */
  .word  DMA1_Stream2_IRQHandler           /* 13 */
  .word  DMA1_Stream3_IRQHandler           /* 14 */
  .word  DMA1_Stream4_IRQHandler           /* 15 */
  .word  DMA1_Stream5_IRQHandler           /* 16 */
  .word  DMA1_Stream6_IRQHandler           /* 17 */
  .word  ADC_IRQHandler                    /* 18 <-- joystick ใช้ตัวนี้ */
  .word  0                                 /* 19 */
  .word  0                                 /* 20 */
  .word  0                                 /* 21 */
  .word  0                                 /* 22 */
  .word  EXTI9_5_IRQHandler                /* 23 */
  .word  TIM1_BRK_TIM9_IRQHandler          /* 24 */
  .word  TIM1_UP_TIM10_IRQHandler          /* 25 */
  .word  TIM1_TRG_COM_TIM11_IRQHandler     /* 26 */
  .word  TIM1_CC_IRQHandler                /* 27 */
  .word  TIM2_IRQHandler                   /* 28 */
  .word  TIM3_IRQHandler                   /* 29 */
  .word  TIM4_IRQHandler                   /* 30 */
  .word  I2C1_EV_IRQHandler                /* 31 */
  .word  I2C1_ER_IRQHandler                /* 32 */
  .word  I2C2_EV_IRQHandler                /* 33 */
  .word  I2C2_ER_IRQHandler                /* 34 */
  .word  SPI1_IRQHandler                   /* 35 */
  .word  SPI2_IRQHandler                   /* 36 */
  .word  USART1_IRQHandler                 /* 37 */
  .word  USART2_IRQHandler                 /* 38 */
  .word  0                                 /* 39 */
  .word  EXTI15_10_IRQHandler              /* 40 */
  .word  RTC_Alarm_IRQHandler              /* 41 */
  .word  OTG_FS_WKUP_IRQHandler            /* 42 */
  .word  0                                 /* 43 */
  .word  0                                 /* 44 */
  .word  0                                 /* 45 */
  .word  0                                 /* 46 */
  .word  DMA1_Stream7_IRQHandler           /* 47 */
  .word  0                                 /* 48 */
  .word  SDIO_IRQHandler                   /* 49 */
  .word  TIM5_IRQHandler                   /* 50 */
  .word  SPI3_IRQHandler                   /* 51 */
  .word  0                                 /* 52 */
  .word  0                                 /* 53 */
  .word  0                                 /* 54 */
  .word  0                                 /* 55 */
  .word  DMA2_Stream0_IRQHandler           /* 56 */
  .word  DMA2_Stream1_IRQHandler           /* 57 */
  .word  DMA2_Stream2_IRQHandler           /* 58 */
  .word  DMA2_Stream3_IRQHandler           /* 59 */
  .word  DMA2_Stream4_IRQHandler           /* 60 */
  .word  0                                 /* 61 */
  .word  0                                 /* 62 */
  .word  0                                 /* 63 */
  .word  0                                 /* 64 */
  .word  0                                 /* 65 */
  .word  0                                 /* 66 */
  .word  OTG_FS_IRQHandler                 /* 67 */
  .word  DMA2_Stream5_IRQHandler           /* 68 */
  .word  DMA2_Stream6_IRQHandler           /* 69 */
  .word  DMA2_Stream7_IRQHandler           /* 70 */
  .word  USART6_IRQHandler                 /* 71 */
  .word  I2C3_EV_IRQHandler                /* 72 */
  .word  I2C3_ER_IRQHandler                /* 73 */
  .word  0                                 /* 74 */
  .word  0                                 /* 75 */
  .word  0                                 /* 76 */
  .word  0                                 /* 77 */
  .word  0                                 /* 78 */
  .word  0                                 /* 79 */
  .word  0                                 /* 80 */
  .word  FPU_IRQHandler                    /* 81 */
  .word  0                                 /* 82 */
  .word  0                                 /* 83 */
  .word  SPI4_IRQHandler                   /* 84 */
  .word  SPI5_IRQHandler                   /* 85 */

  .size  g_pfnVectors, .-g_pfnVectors

/******************************************************************************
 * weak alias: ถ้าไม่มีใครนิยาม handler ตัวไหน จะชี้ไป Default_Handler
 ******************************************************************************/

  .weak      NMI_Handler
  .thumb_set NMI_Handler,Default_Handler

  .weak      HardFault_Handler
  .thumb_set HardFault_Handler,Default_Handler

  .weak      MemManage_Handler
  .thumb_set MemManage_Handler,Default_Handler

  .weak      BusFault_Handler
  .thumb_set BusFault_Handler,Default_Handler

  .weak      UsageFault_Handler
  .thumb_set UsageFault_Handler,Default_Handler

  .weak      SVC_Handler
  .thumb_set SVC_Handler,Default_Handler

  .weak      DebugMon_Handler
  .thumb_set DebugMon_Handler,Default_Handler

  .weak      PendSV_Handler
  .thumb_set PendSV_Handler,Default_Handler

  .weak      SysTick_Handler
  .thumb_set SysTick_Handler,Default_Handler

  .weak      WWDG_IRQHandler
  .thumb_set WWDG_IRQHandler,Default_Handler

  .weak      PVD_IRQHandler
  .thumb_set PVD_IRQHandler,Default_Handler

  .weak      TAMP_STAMP_IRQHandler
  .thumb_set TAMP_STAMP_IRQHandler,Default_Handler

  .weak      RTC_WKUP_IRQHandler
  .thumb_set RTC_WKUP_IRQHandler,Default_Handler

  .weak      FLASH_IRQHandler
  .thumb_set FLASH_IRQHandler,Default_Handler

  .weak      RCC_IRQHandler
  .thumb_set RCC_IRQHandler,Default_Handler

  .weak      EXTI0_IRQHandler
  .thumb_set EXTI0_IRQHandler,Default_Handler

  .weak      EXTI1_IRQHandler
  .thumb_set EXTI1_IRQHandler,Default_Handler

  .weak      EXTI2_IRQHandler
  .thumb_set EXTI2_IRQHandler,Default_Handler

  .weak      EXTI3_IRQHandler
  .thumb_set EXTI3_IRQHandler,Default_Handler

  .weak      EXTI4_IRQHandler
  .thumb_set EXTI4_IRQHandler,Default_Handler

  .weak      DMA1_Stream0_IRQHandler
  .thumb_set DMA1_Stream0_IRQHandler,Default_Handler

  .weak      DMA1_Stream1_IRQHandler
  .thumb_set DMA1_Stream1_IRQHandler,Default_Handler

  .weak      DMA1_Stream2_IRQHandler
  .thumb_set DMA1_Stream2_IRQHandler,Default_Handler

  .weak      DMA1_Stream3_IRQHandler
  .thumb_set DMA1_Stream3_IRQHandler,Default_Handler

  .weak      DMA1_Stream4_IRQHandler
  .thumb_set DMA1_Stream4_IRQHandler,Default_Handler

  .weak      DMA1_Stream5_IRQHandler
  .thumb_set DMA1_Stream5_IRQHandler,Default_Handler

  .weak      DMA1_Stream6_IRQHandler
  .thumb_set DMA1_Stream6_IRQHandler,Default_Handler

  .weak      ADC_IRQHandler
  .thumb_set ADC_IRQHandler,Default_Handler

  .weak      EXTI9_5_IRQHandler
  .thumb_set EXTI9_5_IRQHandler,Default_Handler

  .weak      TIM1_BRK_TIM9_IRQHandler
  .thumb_set TIM1_BRK_TIM9_IRQHandler,Default_Handler

  .weak      TIM1_UP_TIM10_IRQHandler
  .thumb_set TIM1_UP_TIM10_IRQHandler,Default_Handler

  .weak      TIM1_TRG_COM_TIM11_IRQHandler
  .thumb_set TIM1_TRG_COM_TIM11_IRQHandler,Default_Handler

  .weak      TIM1_CC_IRQHandler
  .thumb_set TIM1_CC_IRQHandler,Default_Handler

  .weak      TIM2_IRQHandler
  .thumb_set TIM2_IRQHandler,Default_Handler

  .weak      TIM3_IRQHandler
  .thumb_set TIM3_IRQHandler,Default_Handler

  .weak      TIM4_IRQHandler
  .thumb_set TIM4_IRQHandler,Default_Handler

  .weak      I2C1_EV_IRQHandler
  .thumb_set I2C1_EV_IRQHandler,Default_Handler

  .weak      I2C1_ER_IRQHandler
  .thumb_set I2C1_ER_IRQHandler,Default_Handler

  .weak      I2C2_EV_IRQHandler
  .thumb_set I2C2_EV_IRQHandler,Default_Handler

  .weak      I2C2_ER_IRQHandler
  .thumb_set I2C2_ER_IRQHandler,Default_Handler

  .weak      SPI1_IRQHandler
  .thumb_set SPI1_IRQHandler,Default_Handler

  .weak      SPI2_IRQHandler
  .thumb_set SPI2_IRQHandler,Default_Handler

  .weak      USART1_IRQHandler
  .thumb_set USART1_IRQHandler,Default_Handler

  .weak      USART2_IRQHandler
  .thumb_set USART2_IRQHandler,Default_Handler

  .weak      EXTI15_10_IRQHandler
  .thumb_set EXTI15_10_IRQHandler,Default_Handler

  .weak      RTC_Alarm_IRQHandler
  .thumb_set RTC_Alarm_IRQHandler,Default_Handler

  .weak      OTG_FS_WKUP_IRQHandler
  .thumb_set OTG_FS_WKUP_IRQHandler,Default_Handler

  .weak      DMA1_Stream7_IRQHandler
  .thumb_set DMA1_Stream7_IRQHandler,Default_Handler

  .weak      SDIO_IRQHandler
  .thumb_set SDIO_IRQHandler,Default_Handler

  .weak      TIM5_IRQHandler
  .thumb_set TIM5_IRQHandler,Default_Handler

  .weak      SPI3_IRQHandler
  .thumb_set SPI3_IRQHandler,Default_Handler

  .weak      DMA2_Stream0_IRQHandler
  .thumb_set DMA2_Stream0_IRQHandler,Default_Handler

  .weak      DMA2_Stream1_IRQHandler
  .thumb_set DMA2_Stream1_IRQHandler,Default_Handler

  .weak      DMA2_Stream2_IRQHandler
  .thumb_set DMA2_Stream2_IRQHandler,Default_Handler

  .weak      DMA2_Stream3_IRQHandler
  .thumb_set DMA2_Stream3_IRQHandler,Default_Handler

  .weak      DMA2_Stream4_IRQHandler
  .thumb_set DMA2_Stream4_IRQHandler,Default_Handler

  .weak      OTG_FS_IRQHandler
  .thumb_set OTG_FS_IRQHandler,Default_Handler

  .weak      DMA2_Stream5_IRQHandler
  .thumb_set DMA2_Stream5_IRQHandler,Default_Handler

  .weak      DMA2_Stream6_IRQHandler
  .thumb_set DMA2_Stream6_IRQHandler,Default_Handler

  .weak      DMA2_Stream7_IRQHandler
  .thumb_set DMA2_Stream7_IRQHandler,Default_Handler

  .weak      USART6_IRQHandler
  .thumb_set USART6_IRQHandler,Default_Handler

  .weak      I2C3_EV_IRQHandler
  .thumb_set I2C3_EV_IRQHandler,Default_Handler

  .weak      I2C3_ER_IRQHandler
  .thumb_set I2C3_ER_IRQHandler,Default_Handler

  .weak      FPU_IRQHandler
  .thumb_set FPU_IRQHandler,Default_Handler

  .weak      SPI4_IRQHandler
  .thumb_set SPI4_IRQHandler,Default_Handler

  .weak      SPI5_IRQHandler
  .thumb_set SPI5_IRQHandler,Default_Handler
