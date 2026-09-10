#include "obstacle.h"
#include "game_config.h"

void Obstacle_Init(Obstacle_t *obstacle)
{
    obstacle->active = false;
    obstacle->lane = 0U;
    obstacle->y = GAME_OBSTACLE_SPAWN_Y;
}

void Obstacle_Spawn(Obstacle_t *obstacle, uint8_t lane)
{
    if (lane < GAME_LANE_COUNT)
    {
        obstacle->lane = lane;
        obstacle->y = GAME_OBSTACLE_SPAWN_Y;
        obstacle->active = true;
    }
}

void Obstacle_Move(Obstacle_t *obstacle, uint16_t speed)
{
    if (obstacle->active)
    {
        obstacle->y += (int32_t)speed;
    }
}

bool Obstacle_IsOffRoad(const Obstacle_t *obstacle)
{
    /* y is its TOP edge: at this point the whole obstacle left the road. */
    return obstacle->active && (obstacle->y >= GAME_WORLD_HEIGHT);
}
