#include "player.h"
#include "game_config.h"

static void Player_Move(Player_t *player, GameDirection_t direction)
{
    if ((direction == GAME_DIRECTION_LEFT) && (player->lane > 0U))
    {
        player->lane--;
    }
    else if ((direction == GAME_DIRECTION_RIGHT) &&
             (player->lane < (GAME_LANE_COUNT - 1U)))
    {
        player->lane++;
    }
    else
    {
        /* Center, or already at the road boundary. */
    }
}

void Player_Init(Player_t *player)
{
    player->lane = GAME_START_LANE;
    player->previous_direction = GAME_DIRECTION_CENTER;
    player->repeat_ticks = 0U;
}

void Player_Update(Player_t *player, GameDirection_t direction)
{
    /* Treat an invalid direction as neutral. */
    if ((direction != GAME_DIRECTION_LEFT) &&
        (direction != GAME_DIRECTION_RIGHT))
    {
        player->previous_direction = GAME_DIRECTION_CENTER;
        player->repeat_ticks = 0U;
    }
    else
    {
        if (direction != player->previous_direction)
        {
            Player_Move(player, direction);
            player->repeat_ticks = 0U;
        }
        else
        {
            player->repeat_ticks++;
            if (player->repeat_ticks >= GAME_STEER_REPEAT_TICKS)
            {
                Player_Move(player, direction);
                player->repeat_ticks = 0U;
            }
        }
        player->previous_direction = direction;
    }
}
