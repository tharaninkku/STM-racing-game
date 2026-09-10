#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    GAME_DIRECTION_CENTER = 0,
    GAME_DIRECTION_LEFT,
    GAME_DIRECTION_RIGHT
} GameDirection_t;

typedef enum
{
    GAME_STATE_MENU = 0,
    GAME_STATE_PLAYING,
    GAME_STATE_PAUSED,
    GAME_STATE_GAME_OVER
} GameState_t;

typedef struct
{
    GameDirection_t direction;
    /* A debounced press EVENT, true for one call only, not a held level. */
    bool button_pressed;
} GameInput_t;

typedef struct
{
    uint8_t lane;
    GameDirection_t previous_direction;
    uint16_t repeat_ticks;
} Player_t;

typedef struct
{
    bool active;
    uint8_t lane;
    int32_t y;
} Obstacle_t;

/* Returned by Game_Update for later display/UART/LED integration. */
typedef uint32_t GameEvents_t;
#define GAME_EVENT_NONE             (UINT32_C(0))
#define GAME_EVENT_STARTED          (UINT32_C(1) << 0U)
#define GAME_EVENT_PAUSED           (UINT32_C(1) << 1U)
#define GAME_EVENT_RESUMED          (UINT32_C(1) << 2U)
#define GAME_EVENT_SCORE_CHANGED    (UINT32_C(1) << 3U)
#define GAME_EVENT_LEVEL_CHANGED    (UINT32_C(1) << 4U)
#define GAME_EVENT_COLLISION        (UINT32_C(1) << 5U)
#define GAME_EVENT_GAME_OVER        (UINT32_C(1) << 6U)

#endif
