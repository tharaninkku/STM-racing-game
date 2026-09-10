#include "game.h"
#include "player.h"
#include "obstacle.h"
#include "collision.h"

static void Game_ResetRun(Game_t *game)
{
    uint32_t index;

    Player_Init(&game->player);
    for (index = 0U; index < GAME_MAX_OBSTACLES; index++)
    {
        Obstacle_Init(&game->obstacles[index]);
    }
    game->score = 0U;
    game->level = 1U;
    game->speed = GAME_INITIAL_SPEED;
    game->spawn_interval_ticks = GAME_INITIAL_SPAWN_TICKS;
    game->spawn_ticks = 0U;
}

static uint8_t Game_RandomLane(Game_t *game)
{
    uint32_t value = game->random_state;

    /* Small deterministic xorshift PRNG, never seeded from hardware here. */
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    game->random_state = value;
    return (uint8_t)(value % GAME_LANE_COUNT);
}

static void Game_SpawnObstacle(Game_t *game)
{
    uint32_t index;
    bool spawned = false;

    for (index = 0U; index < GAME_MAX_OBSTACLES; index++)
    {
        if ((!spawned) && (!game->obstacles[index].active))
        {
            Obstacle_Spawn(&game->obstacles[index], Game_RandomLane(game));
            spawned = true;
        }
    }
    /* If the fixed pool is full, skip this spawn; never overwrite a car. */
}

static GameEvents_t Game_UpdateDifficulty(Game_t *game)
{
    uint32_t level = (game->score / GAME_SCORE_PER_LEVEL) + 1U;
    GameEvents_t events = GAME_EVENT_NONE;

    if (level > GAME_MAX_LEVEL)
    {
        level = GAME_MAX_LEVEL;
    }
    if (level != game->level)
    {
        game->level = (uint8_t)level;
        game->speed = (uint16_t)(GAME_INITIAL_SPEED +
                                ((level - 1U) * GAME_SPEED_PER_LEVEL));
        game->spawn_interval_ticks = (uint16_t)(GAME_INITIAL_SPAWN_TICKS -
                              ((level - 1U) * GAME_SPAWN_REDUCTION_TICKS));
        events = GAME_EVENT_LEVEL_CHANGED;
    }
    return events;
}

static GameEvents_t Game_HandleButton(Game_t *game)
{
    GameEvents_t events = GAME_EVENT_NONE;

    switch (game->state)
    {
        case GAME_STATE_MENU:
        case GAME_STATE_GAME_OVER:
            Game_ResetRun(game);
            game->state = GAME_STATE_PLAYING;
            Game_SpawnObstacle(game);
            events = GAME_EVENT_STARTED;
            break;

        case GAME_STATE_PLAYING:
            game->state = GAME_STATE_PAUSED;
            events = GAME_EVENT_PAUSED;
            break;

        case GAME_STATE_PAUSED:
            game->state = GAME_STATE_PLAYING;
            events = GAME_EVENT_RESUMED;
            break;

        default:
            /* A valid initialized game only uses the four states above. */
            break;
    }
    return events;
}

static GameEvents_t Game_Advance(Game_t *game, GameDirection_t direction)
{
    uint32_t index;
    bool hit = false;
    GameEvents_t events = GAME_EVENT_NONE;

    Player_Update(&game->player, direction);
    for (index = 0U; index < GAME_MAX_OBSTACLES; index++)
    {
        Obstacle_t *obstacle = &game->obstacles[index];
        int32_t previous_y = obstacle->y;

        Obstacle_Move(obstacle, game->speed);
        if (Collision_HasHit(&game->player, obstacle, previous_y))
        {
            hit = true;
        }
    }

    /* Collision wins over scoring/spawning, independent of array order. */
    if (hit)
    {
        game->state = GAME_STATE_GAME_OVER;
        events = GAME_EVENT_COLLISION | GAME_EVENT_GAME_OVER;
    }
    else
    {
        for (index = 0U; index < GAME_MAX_OBSTACLES; index++)
        {
            if (Obstacle_IsOffRoad(&game->obstacles[index]))
            {
                Obstacle_Init(&game->obstacles[index]);
                /* Saturate instead of wrapping the score to zero. */
                if (game->score < UINT32_MAX)
                {
                    game->score++;
                    events |= GAME_EVENT_SCORE_CHANGED;
                }
            }
        }
        events |= Game_UpdateDifficulty(game);
        game->spawn_ticks++;
        if (game->spawn_ticks >= game->spawn_interval_ticks)
        {
            game->spawn_ticks = 0U;
            Game_SpawnObstacle(game);
        }
    }
    return events;
}

void Game_Init(Game_t *game, uint32_t seed)
{
    Game_ResetRun(game);
    game->state = GAME_STATE_MENU;
    game->random_state = (seed == 0U) ? GAME_DEFAULT_SEED : seed;
}

GameEvents_t Game_Update(Game_t *game, GameInput_t input)
{
    GameEvents_t events = GAME_EVENT_NONE;

    if (input.button_pressed)
    {
        events = Game_HandleButton(game);
    }
    else if (game->state == GAME_STATE_PLAYING)
    {
        events = Game_Advance(game, input.direction);
    }
    else
    {
        /* MENU, PAUSED and GAME_OVER freeze the entire simulation. */
    }
    return events;
}
