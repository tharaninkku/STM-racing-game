#ifndef COLLISION_H
#define COLLISION_H

#include "game_types.h"

/* Downward motion only. previous_y <= obstacle->y; edges touching is safe. */
bool Collision_HasHit(const Player_t *player,
                      const Obstacle_t *obstacle,
                      int32_t previous_y);

#endif
