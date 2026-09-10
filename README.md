# STM32 Mini Racing Game

โปรเจคเกมขับรถหลบสิ่งกีดขวางบน Nucleo-F411RE ตาม Project.pdf

สถานะปัจจุบัน: **เฟิร์มแวร์ build และแฟลชลงบอร์ดได้แล้ว** joystick ทำงานยืนยันบนฮาร์ดแวร์จริง
game logic เสร็จและผ่านเทสต์บน PC แต่ยังไม่ได้เชื่อมเข้ากับ main loop
เพราะ button driver และ display ยังว่าง

## โครงสร้าง

```text
STM32_Mini_Racing_Game/
  .project / .cproject          STM32CubeIDE project configuration
  Application/
    game.c / game.h            state machine, tick, score, difficulty, spawn
    game_config.h              ค่าปรับแต่งเกมและ logical coordinates
    game_types.h               state, input, player, obstacle, event types
    player.c / player.h        เลี้ยวและขอบเขตถนน
    obstacle.c / obstacle.h    สร้าง เลื่อน และตรวจออกจากถนน
    collision.c / collision.h  เลนเดียวกัน + Y overlap แบบ swept interval
    game_render.c / .h         ว่าง — รอเชื่อม display
  Drivers/
    joystick.c / joystick.h    เสร็จแล้ว — ADC1 + EOC interrupt, register level
    button, display, status_led, uart_debug, game_tick    ยังว่าง
  Src/
    main.c                     bring-up ทดสอบ joystick (ยังไม่เรียก game logic)
    stm32f4xx_it.c             SysTick + ADC handler
    system_stm32f4xx.c         SystemInit, SystemCoreClock
    syscalls.c / sysmem.c      ยังว่าง (ใช้ stub จาก nosys.specs ไปก่อน)
  Inc/                         main.h, stm32f4xx_it.h ยังว่าง
  Startup/
    startup_stm32f411retx.s    vector table + Reset_Handler
  STM32F411RETX_FLASH.ld       memory layout จาก Training_Lab5.3
  STM32F411RETX_RAM.ld         memory layout จาก Training_Lab5.3
  Tests/                       ทดสอบ game logic บน PC เท่านั้น
```

อิง metadata จาก Training_Lab5.3 และแก้ชื่อโปรเจค, build path, source folders,
include paths สำหรับโปรเจคใหม่นี้ ทั้ง Debug/Release ใช้ Application, Src, Inc, Startup, Drivers
Tests ไม่อยู่ใน firmware build จึงไม่ชนกับ main.c ของบอร์ด
ไม่มี .ioc เพราะเป็น CMSIS/register project ทั้งหมด ไม่ได้ใช้ HAL

นำเข้าใน STM32CubeIDE ด้วย File > Import > General > Existing Projects into Workspace
เลือกโฟลเดอร์นี้และไม่ต้อง Copy projects into workspace เพราะอยู่ใน workspace แล้ว

## ฮาร์ดแวร์

บอร์ด Nucleo-F411RE (MB1136 rev C) เสียบทับด้วย **STEO Training Shield 1 Rev 02.00**

ขาที่ shield จองไปแล้ว อ่านจาก silkscreen บนตัว shield:

| ใช้ทำอะไร | ขา |
|---|---|
| LDR / thermistor | PA0 (A0), PA1 (A1) |
| Potentiometer | PA4 (A2) |
| LED 4 ดวง | PA5 ฟ้า, PA6 แดง, PA7 เหลือง, PB6/PC9 เขียว |
| ปุ่ม 4 ตัว | PA10, PB3, PB5, PB4 |
| 7-segment BCD | PA8, PA9, PB10, PC6/PC7 |
| I2C (BH1750 / AHT10) | PB8, PB9 |

จอยสติ๊กต่อผ่าน ST morpho CN7 ฝั่งซ้าย ซึ่ง shield ไม่ได้ทับ:

| สายจอย | CN7 pin | ขา MCU | หมายเหตุ |
|---|---|---|---|
| GND | 20 | — | |
| +5V | 16 | +3V3 | **ห้ามใช้ 5V** ขา ADC ไม่ทน |
| VRx | 38 | PC0 | ADC1_IN10 |
| VRy | 36 | PC1 | ADC1_IN11 |
| SW | 35 | PC2 | ยังไม่ได้ใช้ |

CN7 นับเป็น 19 แถว แถวที่ N คือ pin (2N−1) กับ 2N แถว 1 อยู่ฝั่งใกล้ ST-Link
PC0/PC1/PC2/PC3 อยู่ 2 แถวล่างสุดพอดี ยืนยันตาราง morpho จาก UM1724 ก่อนเสียบทุกครั้ง
เพราะเสียบผิดแถวเดียวอาจไปโดน VIN ที่แถว 12

## Clock และ tick

HSI 16 MHz -> PLL (M16, N336, P4) -> SYSCLK 84 MHz, AHB 84 MHz, APB1 42 MHz, APB2 84 MHz
Flash latency 2 wait states, voltage scale 1, เปิด FPU ใน SystemInit

ADC prescaler ตั้ง PCLK2/4 ได้ ADCCLK 21 MHz (เพดานคือ 36 MHz)
**ถ้าเปลี่ยนความถี่ระบบ ต้องกลับไปดู ADC_CCR ใน joystick.c ด้วย**

ตอนนี้ใช้ SysTick เป็นตัวสร้าง game tick ที่ 50 Hz (20 ms ต่อ tick ตรงตามที่ game logic กำหนด)
เป็นของชั่วคราว เพราะ `game_tick.c` ยังว่างและเกณฑ์ใน PDF ต้องการ peripheral timer
main loop ใช้ `tick_pending` เป็น counter ไม่ใช่ bool เพื่อไม่ให้ tick หายเวลา loop ช้า
และลดค่าโดยปิด interrupt ชั่วขณะเพื่อกัน SysTick แทรกกลางคัน

## Joystick driver

`Drivers/joystick.c` เขียนแบบ register level ล้วน ไม่ใช้ HAL ไม่ใช้ DMA

การทำงาน: main loop เรียก `Joystick_StartSample()` หนึ่งครั้งต่อ tick
-> ADC แปลงแกน X -> EOC interrupt -> ISR เก็บค่าแล้วสลับไปแกน Y
-> EOC อีกครั้ง -> ISR ตั้ง flag ว่าพร้อม
ทั้งกระบวนการไม่ block main loop เลย และที่ 50 Hz เกิด interrupt แค่ 100 ครั้งต่อวินาที

- `Joystick_Calibrate()` วัดจุดกลางจริงตอนบูตโดยเฉลี่ย 32 ครั้ง (ห้ามแตะจอยตอนนั้น)
  ถ้า ADC ไม่ตอบภายใน timeout จะถอยไปใช้ 2048 แทนแล้วเดินต่อ ไม่ค้าง
- `Joystick_GetDirection()` คืน LEFT / CENTER / RIGHT พร้อม dead zone และ hysteresis
  เข้าที่ 700 ออกที่ 450 กันทิศกระพริบตอนโยกค้างใกล้เส้นแบ่ง
- driver คืน `Joystick_Dir_t` ของตัวเอง ไม่รู้จัก `game_types.h`
  การ map เป็น `GAME_DIRECTION_*` เป็นงานของ main.c เพื่อไม่ให้ Driver ขึ้นกับ Application
- ขา, channel, threshold, และ `JOY_INVERT_X` รวมอยู่ใน CONFIG block บนหัวไฟล์
  เปลี่ยนขาแก้แค่ตรงนั้น ไม่ต้องรื้อโค้ด

`Joystick_IrqHandler()` เรียกจาก `ADC_IRQHandler()` ใน stm32f4xx_it.c เท่านั้น
ISR ไม่เรียก game logic และไม่มี loop ตามกติกาของโปรเจค

## กติกาที่เลือกสำหรับเวอร์ชันนี้

- 3 เลน (0, 1, 2) เริ่มเลนกลาง รถวิ่งเอง ไม่มีคันเร่ง/เบรก/touch sensor
- `Game_Update` หนึ่งครั้งเท่ากับเวลาเกม 20 ms; ในโมดูลนี้ไม่มี timer หรือ delay
- โยกซ้าย/ขวาแล้วเลื่อน 1 เลนทันที ถ้าค้างไว้จะเลื่อนซ้ำทุก 8 ticks (160 ms)
  กลับ center แล้วโยกใหม่จะเลื่อนทันทีอีกครั้ง การสลับซ้ายเป็นขวาก็มีผลทันที
- เริ่มเกมมี obstacle 1 คันด้านบน แล้วทยอยสุ่มเลนครั้งละ 1 คัน เก็บพร้อมกันได้ 8 คัน
  ถ้าเต็มจะข้ามการเกิดครั้งนั้น ไม่ทับคันที่ยังอยู่
- พ้นด้านล่างทั้งคันได้ 1 คะแนน คะแนนไม่วนกลับเป็นศูนย์เมื่อเต็ม uint32_t
- ทุก 5 คะแนนเพิ่ม level สูงสุด 5: speed = 4/6/8/10/12 logical units ต่อ tick
  ช่วงเกิด obstacle = 60/55/50/45/40 ticks (1.2/1.1/1.0/0.9/0.8 วินาที)
- ถ้าเลนเดียวกันและ Y ทับกันจะ Game Over ตรวจช่วงการเคลื่อนที่ด้วยเพื่อไม่ให้รถเร็วกระโดดข้าม collision
  ขอบแตะกันพอดียังไม่นับชน ถ้าชนและมีคันอื่นผ่านใน tick เดียวกัน ให้การชนมาก่อนและไม่เพิ่มคะแนน
- MENU + ปุ่ม → PLAYING; PLAYING + ปุ่ม → PAUSED;
  PAUSED + ปุ่ม → PLAYING; GAME_OVER + ปุ่ม → เริ่ม PLAYING ใหม่ คะแนน/level reset
- ปุ่มเปลี่ยนสถานะใช้ tick นั้นทั้งหมด ไม่ขยับเกมต่อใน tick เดียวกัน
  ตอน pause หยุดคะแนน ตำแหน่ง spawn timer และ steering repeat timer ทั้งหมด
- seed เดียวกันกับ input เหมือนกันให้ผลซ้ำได้ การ restart เก็บลำดับ PRNG ต่อเพื่อเปลี่ยนชุด obstacle
  เรียก `Game_Init` ด้วย seed เดิมเมื่อต้องการเล่นลำดับเดิมซ้ำ ไม่ได้อ้างว่าเป็น hardware randomness

ค่าตำแหน่ง Y เป็นโลกเกมสูง 1000 หน่วย ไม่ใช่ความละเอียด OLED:
player อยู่ Y=850 สูง 100; obstacle สูง 100 เริ่ม Y=-100; ออกจากถนนเมื่อ top Y >= 1000
Display driver ค่อยแปลงหน่วยเป็นพื้นที่เล่นบนจอที่เลือกจริงและเผื่อพื้นที่ score
ค่าคอนฟิกเหล่านี้ออกแบบร่วมกัน ถ้าแก้ขนาด/ความเร็ว/ช่วงเกิด ให้ทดสอบการชนและระยะหลบใหม่

## เริ่มอ่านโค้ดตรงไหน

1. `game_types.h` และ `game.h`: input, สถานะและข้อมูลทั้งหมดของเกม
2. `Game_Init`: reset ข้อมูลและเริ่มที่ MENU
3. `Game_Update`: จัดการปุ่มก่อน แล้วค่อยเดินเกมถ้ากำลัง PLAYING
4. `Game_Advance`: เลี้ยว → เลื่อน obstacles → ตรวจชน → ให้คะแนน → ปรับ level → spawn
5. `Player_Update`: กันออกนอกเลนและจำเวลาการโยกค้าง
6. `Collision_HasHit`: ตรวจเลนกับช่วง Y ตั้งแต่ก่อนขยับถึงหลังขยับ

`Game_t` เป็นหน่วยความจำที่ผู้เรียกจัดสรรเอง เช่น static variable ใน main.c ภายหลัง
ไม่มี malloc, global mutable state, floating point หรือ HAL ใน game logic
ทุก pointer ที่ส่งให้ฟังก์ชันต้องเป็น object ที่ถูกต้องและ initialize แล้ว
โค้ด integration อ่าน fields เพื่อแสดงผลได้ แต่ไม่ควรแก้ fields โดยตรง

ตัวอย่างเฉพาะการเรียก API (ยังไม่ได้ใส่ใน main.c):

```c
static Game_t game;
Game_Init(&game, 123U); /* ก่อนเริ่ม main loop */

/* main loop: เมื่อรับ fixed tick หนึ่งครั้ง */
GameInput_t input = { GAME_DIRECTION_CENTER, false };
/* direction มาจาก Joystick_GetDirection() แล้ว map เป็น GAME_DIRECTION_* */
/* button_pressed ยังรอ button driver */
GameEvents_t events = Game_Update(&game, input);
```

`button_pressed` คือเหตุการณ์กดที่ผ่าน debounce แล้ว ให้ true เพียงหนึ่งครั้งต่อการกดจริง
ถ้าส่งระดับปุ่มค้างเป็น true ทุก tick เกมจะสลับ pause/resume ทุกครั้ง ซึ่งผิดสัญญา API
API ของ button driver จึงควรเป็นแบบหยิบแล้วหาย และ debounce ด้วยการเทียบ tick ไม่ใช่ delay ใน ISR

ค่าที่คืนจาก Update เป็น bit flags เช่น `GAME_EVENT_COLLISION | GAME_EVENT_GAME_OVER`
ใช้ `(events & GAME_EVENT_COLLISION) != 0U` ตรวจ แล้วอ่าน `game.score`, `game.level`, `game.state`
สำหรับรายละเอียด event แต่ละผลลัพธ์มีอายุหนึ่ง call: integration ต้องรับ/ส่งต่อก่อน update ถัดไป
หากรอหลาย ticks ให้เรียก Update ต่อ tick และส่ง press event เพียงครั้งเดียว
ISR แค่รับข้อมูล/แจ้งเหตุการณ์; เรียก game logic และอ่าน state จาก main loop เท่านั้น

## ทดสอบ

### game logic บน PC

จาก PowerShell ที่มี GCC:

```powershell
& 'D:/STProject/STM32_Mini_Racing_Game/Tests/run_tests.ps1'
```

หรือระบุ compiler:

```powershell
& 'D:/STProject/STM32_Mini_Racing_Game/Tests/run_tests.ps1' -Compiler 'D:/w64devkit/w64devkit/bin/gcc.exe'
```

Tests ตรวจ state transitions/freeze, ขอบเลนและ repeat, collision boundaries,
restart, scoring ครั้งเดียว, difficulty cap, score overflow, spawn timing/pool เต็ม,
การเล่นหลบจริง 12,000 ticks และ deterministic replay 100,000 ticks

ผลตรวจ 6 กันยายน 2026: ผ่านทั้ง 7 กลุ่มทดสอบ และคอมไพล์ `game.c`, `player.c`,
`obstacle.c`, `collision.c` เป็น Cortex-M4 objects ด้วย ARM GCC ที่ติดตั้งมากับ CubeIDE
โดยเปิด `-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes`

### joystick บนบอร์ดจริง

`Src/main.c` ตอนนี้เป็นโปรแกรมทดสอบ ไม่ได้เรียก game logic

| การกระทำ | ผลที่คาดหวัง |
|---|---|
| โยกซ้าย | ไฟแดง D12 (PA6) ติด |
| โยกขวา | ไฟเหลือง D11 (PA7) ติด |
| ปล่อยมือ | ดับทั้งคู่ |

ผลตรวจ 10 กันยายน 2026: ผ่าน ยืนยันว่า PLL 84 MHz, vector table, SCB->VTOR,
NVIC, ADC EOC interrupt, SysTick และ hysteresis ทำงานถูกต้องบนฮาร์ดแวร์จริง

อาการที่เจอบ่อยและวิธีแก้:

| อาการ | สาเหตุ |
|---|---|
| ไฟติดสลับข้าง | ตั้ง `JOY_INVERT_X` เป็น 1 |
| ไฟกระพริบเองตอนไม่โยก | เพิ่ม `JOY_THRESHOLD_ENTER` |
| ไฟไม่ติดเลย ค้างตั้งแต่บูต | `ADC_IRQHandler` ไม่ได้เรียก `Joystick_IrqHandler` |
| ไฟติดค้างข้างเดียว | สายจอยหรือ GND ไม่ถึง วัดแรงดันที่ VRx |

## ข้อควรระวังของ build ปัจจุบัน

- `startup_stm32f411retx.s` เขียนขึ้นเองตาม reference manual ไม่ใช่ไฟล์ที่ ST generate
  ตาราง vector ยังไม่ได้ตรวจทีละช่องกับเอกสาร ถ้าเรียงผิดตำแหน่งเดียว interrupt
  จะเข้าผิดตัวโดยไม่มี error ให้เห็น พิจารณาเปลี่ยนไปใช้ไฟล์จาก CubeIDE เมื่อสะดวก
- `system_stm32f4xx.c` ต้องมีแค่ `SystemInit()` กับ `SystemCoreClock`
  ห้ามใส่ interrupt handler เพราะจะชนกับ stm32f4xx_it.c เป็น multiple definition
- warning `_close / _lseek / _read / _write is not implemented` เป็นเรื่องปกติ
  เพราะ syscalls.c ยังว่างและใช้ stub จาก nosys.specs
  CubeIDE นับ warning เหล่านี้เป็น error ในสรุปท้าย build ทั้งที่ .elf สร้างสำเร็จ
  ให้ดูบรรทัด `Finished building target` เป็นเกณฑ์แทน
- ยังไม่ได้ตรวจว่า `.isr_vector` ถูกวางที่ 0x08000000 ในไฟล์ .map

## งานที่ยังเว้นให้ทำต่อ

ลำดับที่วางไว้:

1. **`button.c`** — EXTI + debounce บนปุ่มของ shield (PA10, PB3, PB5, PB4)
   API แบบหยิบแล้วหายเพื่อให้ตรงสัญญาของ `button_pressed`
   ยังไม่ได้เลือกว่าจะใช้ปุ่มตัวไหน
2. **`game_tick.c`** — ย้าย tick จาก SysTick ไปใช้ timer peripheral ตามเกณฑ์ใน PDF
   priority ของ ADC ต้องต่ำกว่า tick timer เสมอ (ตอนนี้ตั้งไว้ 6)
3. **เชื่อม game logic เข้า main loop** แทนโปรแกรมทดสอบ joystick
4. **display** — ยังไม่ได้ยืนยันรุ่น OLED, controller, resolution, interface
   ถ้าใช้ I2C ต้องเช็คว่า address ไม่ชนกับ BH1750 และ AHT10 บน shield ที่ใช้ PB8/PB9 อยู่
5. **`uart_debug.c`** — UART Interrupt หรือ DMA ตามเกณฑ์ ถ้าจะใช้ printf ต้องเติม syscalls.c
6. **`status_led.c`** — แจ้ง collision / game over
7. **`game_render.c`** — วาดเกมลงจอ

ตามเกณฑ์ใน PDF: GPIO, UART Interrupt/DMA เท่านั้น, ADC Interrupt/DMA เท่านั้น,
EXTI อย่างน้อยหนึ่งจุด, peripheral เพิ่มอย่างน้อยหนึ่งชนิด, แยก Application/Driver,
และ MISRA-C อย่างน้อย 22 rules ตาม check sheet ของวิชา

ทำแล้ว: ADC เป็น interrupt, แยก Application/Driver ชัดเจน, GPIO
ยังขาด: EXTI, UART, peripheral timer, และ check sheet 22 rules
จึงยังไม่อ้างว่าโค้ดผ่าน MISRA-C แม้จะตรวจ compiler warnings แล้ว

7-segment บน shield แสดงได้หลักเดียว ถ้าจะใช้แสดงคะแนนต้องคิดเรื่องคะแนนเกิน 9
และตัวเกมยังต้องมีจอแยกอยู่ดี