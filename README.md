# STM32 Mini Racing Game — game logic milestone

โปรเจคเกมขับรถหลบสิ่งกีดขวางบน Nucleo-F411RE ตาม Project.pdf และบริบทที่แนบมา
รอบนี้เขียนเฉพาะ game logic ตามคำขอล่าสุด ส่วน peripheral, rendering และ main loop เป็นไฟล์ว่างจริง (0 bytes)
ข้อความในเอกสารที่ให้เริ่มจากการสอน peripheral ทีละ milestone ใช้เป็นบริบท ไม่ได้ใช้แทนคำขอครั้งนี้

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
  Drivers/                     .c/.h ทุกคู่ยังว่าง
    joystick, button, display, status_led, uart_debug, game_tick
  Src/                         main.c, stm32f4xx_it.c, system_stm32f4xx.c,
                               syscalls.c, sysmem.c ยังว่าง
  Inc/                         main.h, stm32f4xx_it.h ยังว่าง
  Startup/                     startup_stm32f411retx.s ยังว่าง
  STM32F411RETX_FLASH.ld        memory layout จาก Training_Lab5.3
  STM32F411RETX_RAM.ld          memory layout จาก Training_Lab5.3
  Tests/                       ทดสอบ game logic บน PC เท่านั้น
```

อิง metadata จาก Training_Lab5.3 และแก้ชื่อโปรเจค, build path, source folders,
include paths สำหรับโปรเจคใหม่นี้ ทั้ง Debug/Release ใช้ Application, Src, Inc, Startup, Drivers
Tests ไม่อยู่ใน firmware build จึงไม่ชนกับ main.c ของบอร์ด
ไม่ได้คัดลอกโค้ด lab หรือแก้ไฟล์โปรเจคเดิม
ไม่มี .ioc เพราะโปรเจคตัวอย่างเป็น CMSIS/register project และยังไม่ได้กำหนด peripheral

นำเข้าใน STM32CubeIDE ด้วย File > Import > General > Existing Projects into Workspace
เลือกโฟลเดอร์นี้และไม่ต้อง Copy projects into workspace เพราะอยู่ใน workspace แล้ว
ทดสอบ import ผ่าน STM32CubeIDE 2.2.0 แบบ headless ใน workspace ชั่วคราวแล้ว
แต่ยังไม่ได้เพิ่มลงรายการโปรเจคของ IDE ที่คุณเปิดอยู่

**ยัง Build เป็นเฟิร์มแวร์/แฟลชลงบอร์ดไม่ได้** เพราะ main, startup และ system initialization
ยังว่างตาม scope การคอมไพล์ game logic เป็น object หรือรัน Tests ทำได้แยกต่างหาก
Linker scripts เป็น configuration ของ memory layout ไม่ใช่การตั้งค่า clock/pin/peripheral

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
/* ภายหลัง: เติม direction จาก joystick driver และ event จาก button driver */
GameEvents_t events = Game_Update(&game, input);
/* ภายหลัง: render game และส่ง events ให้ UART/LED */
```

`button_pressed` คือเหตุการณ์กดที่ผ่าน debounce แล้ว ให้ true เพียงหนึ่งครั้งต่อการกดจริง
ถ้าส่งระดับปุ่มค้างเป็น true ทุก tick เกมจะสลับ pause/resume ทุกครั้ง ซึ่งผิดสัญญา API
การ map ADC เป็น LEFT/CENTER/RIGHT และ debounce เป็นงานของ driver ที่ยังว่าง

ค่าที่คืนจาก Update เป็น bit flags เช่น `GAME_EVENT_COLLISION | GAME_EVENT_GAME_OVER`
ใช้ `(events & GAME_EVENT_COLLISION) != 0U` ตรวจ แล้วอ่าน `game.score`, `game.level`, `game.state`
สำหรับรายละเอียด event แต่ละผลลัพธ์มีอายุหนึ่ง call: integration ต้องรับ/ส่งต่อก่อน update ถัดไป
หากรอหลาย ticks ให้เรียก Update ต่อ tick และส่ง press event เพียงครั้งเดียว
ควรใช้ pending tick counter ที่รับ/ลดค่าอย่างปลอดภัยเมื่อเชื่อม ISR เพื่อไม่ให้ tick หายจากการใช้ bool flag
ISR แค่รับข้อมูล/แจ้งเหตุการณ์; เรียก game logic และอ่าน state จาก main loop เท่านั้น

## ทดสอบ

จาก PowerShell ที่มี GCC:

```powershell
& 'D:/STM32_Workspace/STM32_Mini_Racing_Game/Tests/run_tests.ps1'
```

หรือระบุ compiler:

```powershell
& 'D:/STM32_Workspace/STM32_Mini_Racing_Game/Tests/run_tests.ps1' -Compiler 'D:/w64devkit/w64devkit/bin/gcc.exe'
```

Tests ตรวจ state transitions/freeze, ขอบเลนและ repeat, collision boundaries,
restart, scoring ครั้งเดียว, difficulty cap, score overflow, spawn timing/pool เต็ม,
การเล่นหลบจริง 12,000 ticks และ deterministic replay 100,000 ticks
การทดสอบบน PC เป็นเพียงการยืนยัน logic; เกมจริงจะประมวลผลบน MCU หลังเชื่อม hardware แล้ว

ผลตรวจวันที่ 6 กันยายน 2026: ผ่านทั้ง 7 กลุ่มทดสอบ และคอมไพล์ `game.c`, `player.c`,
`obstacle.c`, `collision.c` เป็น Cortex-M4 objects ด้วย ARM GCC ที่ติดตั้งมากับ CubeIDE 2.2.0
โดยเปิด `-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes`
ตรวจไฟล์ว่างครบ 22 ไฟล์แล้ว ผลนี้ยังไม่รวม firmware linking หรือการทดสอบบนบอร์ด

## งานที่ยังเว้นให้ทำต่อ

ยืนยัน OLED รุ่น/controller/resolution/interface, joystick รุ่นและ calibration,
ปุ่มที่จะใช้, pin mapping, ADC channel, timer instance, UART instance/baud rate และ I2C pins
แล้วค่อยทำ startup/system clock, main loop, ADC + DMA, EXTI + debounce,
timer interrupt, OLED/I2C, UART Interrupt/DMA และ GPIO LED
ฝั่ง game ส่ง collision/game-over events แล้ว แต่ยังไม่มีการวาด ส่ง UART หรือกระพริบ LED

ตามเกณฑ์ใน PDF: GPIO, UART Interrupt/DMA เท่านั้น, ADC Interrupt/DMA เท่านั้น,
EXTI อย่างน้อยหนึ่งจุด, peripheral เพิ่มอย่างน้อยหนึ่งชนิด, แยก Application/Driver,
และ MISRA-C อย่างน้อย 22 rules ตาม check sheet ของวิชา
โครงสร้างเกมนี้เตรียมให้แยก driver ได้ แต่ยังไม่ได้ทำ peripheral requirements ให้ครบ
ยังไม่มี check sheet 22 rules จึงยังไม่อ้างว่าโค้ดผ่าน MISRA-C แม้จะตรวจ compiler warnings แล้ว
