#ifndef OBSTACLE_H
#define OBSTACLE_H

#include "game_types.h"

void Obstacle_Init(Obstacle_t *obstacle);
void Obstacle_Spawn(Obstacle_t *obstacle, uint8_t lane);
void Obstacle_Move(Obstacle_t *obstacle, uint16_t speed);
bool Obstacle_IsOffRoad(const Obstacle_t *obstacle);

#endif
