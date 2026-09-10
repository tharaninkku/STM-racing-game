#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

/* Logical units, NOT OLED pixels. The display driver will scale these later. */
#define GAME_LANE_COUNT             (3U)
#define GAME_START_LANE             (1U)
#define GAME_WORLD_HEIGHT           (1000)
#define GAME_PLAYER_Y               (850)
#define GAME_PLAYER_HEIGHT          (100)
#define GAME_OBSTACLE_HEIGHT        (100)
#define GAME_OBSTACLE_SPAWN_Y        (-GAME_OBSTACLE_HEIGHT)
#define GAME_MAX_OBSTACLES           (8U)

/* One Game_Update call represents one fixed tick; no hardware timer here. */
#define GAME_TICK_MS                (20U)
#define GAME_STEER_REPEAT_TICKS      (8U)
#define GAME_SCORE_PER_LEVEL         (5U)
#define GAME_MAX_LEVEL              (5U)
#define GAME_INITIAL_SPEED          (4U)
#define GAME_SPEED_PER_LEVEL        (2U)
#define GAME_INITIAL_SPAWN_TICKS    (60U)
#define GAME_SPAWN_REDUCTION_TICKS   (5U)
#define GAME_DEFAULT_SEED           (1U)

#endif
