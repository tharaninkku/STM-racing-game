#ifndef GAME_H
#define GAME_H

#include "game_config.h"
#include "game_types.h"

/* Caller-owned storage; read fields for rendering, do not modify them.
 * No dynamic allocation, HAL, GPIO, ADC, display or UART dependency.
 * All calls and state reads belong to the main loop, not concurrent ISRs.
 */
typedef struct
{
    GameState_t state;
    Player_t player;
    Obstacle_t obstacles[GAME_MAX_OBSTACLES];
    uint32_t score;
    uint8_t level;
    uint16_t speed;
    uint16_t spawn_interval_ticks;
    uint16_t spawn_ticks;
    uint32_t random_state;
} Game_t;

/* Call once before Update. Zero seed uses GAME_DEFAULT_SEED.
 * Same seed and input sequence reproduce the same game for debugging.
 */
void Game_Init(Game_t *game, uint32_t seed);

/* Call exactly once per GAME_TICK_MS of simulation time.
 * Button transitions consume this tick (no motion or scoring).
 * GAME_OVER + press immediately starts a new run with score zero.
 * Events describe THIS call only; consume each result before the next call.
 */
GameEvents_t Game_Update(Game_t *game, GameInput_t input);

#endif
