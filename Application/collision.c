#include "collision.h"
#include "game_config.h"

bool Collision_HasHit(const Player_t *player,
                      const Obstacle_t *obstacle,
                      int32_t previous_y)
{
    /* Sweep the Y interval so a fast obstacle cannot jump over the player. */
    return obstacle->active &&
           (player->lane == obstacle->lane) &&
           (previous_y < (GAME_PLAYER_Y + GAME_PLAYER_HEIGHT)) &&
           ((obstacle->y + GAME_OBSTACLE_HEIGHT) > GAME_PLAYER_Y);
}
