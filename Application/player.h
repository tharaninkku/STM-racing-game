#ifndef PLAYER_H
#define PLAYER_H

#include "game_types.h"

/* All module pointers must refer to valid objects initialized by their Init. */
void Player_Init(Player_t *player);
void Player_Update(Player_t *player, GameDirection_t direction);

#endif
