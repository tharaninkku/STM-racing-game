#include <assert.h>
#include <stdio.h>
#include "game.h"
#include "player.h"
#include "obstacle.h"
#include "collision.h"

static GameEvents_t Tick(Game_t *game, GameDirection_t direction, bool pressed)
{
    const GameInput_t input = {direction, pressed};
    return Game_Update(game, input);
}

static void Start(Game_t *game)
{
    Game_Init(game, 123U);
    assert(Tick(game, GAME_DIRECTION_CENTER, true) == GAME_EVENT_STARTED);
}

/* Tests may edit state deliberately to arrange precise boundary cases.
 * Production integration must only read state and call Game_Init/Update.
 */
static void ClearObstacles(Game_t *game)
{
    uint32_t i;
    for (i = 0U; i < GAME_MAX_OBSTACLES; i++)
    {
        Obstacle_Init(&game->obstacles[i]);
    }
}

static void AssertSame(const Game_t *a, const Game_t *b)
{
    uint32_t i;
    assert(a->state == b->state);
    assert(a->score == b->score);
    assert(a->level == b->level);
    assert(a->speed == b->speed);
    assert(a->spawn_ticks == b->spawn_ticks);
    assert(a->spawn_interval_ticks == b->spawn_interval_ticks);
    assert(a->random_state == b->random_state);
    assert(a->player.lane == b->player.lane);
    assert(a->player.repeat_ticks == b->player.repeat_ticks);
    assert(a->player.previous_direction == b->player.previous_direction);
    for (i = 0U; i < GAME_MAX_OBSTACLES; i++)
    {
        assert(a->obstacles[i].active == b->obstacles[i].active);
        assert(a->obstacles[i].lane == b->obstacles[i].lane);
        assert(a->obstacles[i].y == b->obstacles[i].y);
    }
}

static void TestStates(void)
{
    Game_t game;
    Game_t saved;
    uint32_t i;
    Game_Init(&game, 0U);
    assert(game.state == GAME_STATE_MENU);
    assert(game.random_state == GAME_DEFAULT_SEED);
    saved = game;
    assert(Tick(&game, GAME_DIRECTION_LEFT, false) == GAME_EVENT_NONE);
    AssertSame(&game, &saved);
    assert(Tick(&game, GAME_DIRECTION_RIGHT, true) == GAME_EVENT_STARTED);
    assert(game.player.lane == GAME_START_LANE);
    assert(game.obstacles[0].active);
    assert(game.obstacles[0].y == GAME_OBSTACLE_SPAWN_Y);
    assert(Tick(&game, GAME_DIRECTION_LEFT, true) == GAME_EVENT_PAUSED);
    saved = game;
    for (i = 0U; i < 1000U; i++)
    {
        assert(Tick(&game, GAME_DIRECTION_RIGHT, false) == GAME_EVENT_NONE);
    }
    AssertSame(&game, &saved);
    assert(Tick(&game, GAME_DIRECTION_RIGHT, true) == GAME_EVENT_RESUMED);
    saved.state = GAME_STATE_PLAYING;
    AssertSame(&game, &saved);
}

static void TestSteering(void)
{
    Player_t player;
    uint32_t i;
    Player_Init(&player);
    Player_Update(&player, GAME_DIRECTION_LEFT);
    assert(player.lane == 0U);
    for (i = 0U; i < 1000U; i++)
    {
        Player_Update(&player, GAME_DIRECTION_LEFT);
    }
    assert(player.lane == 0U);
    Player_Update(&player, GAME_DIRECTION_RIGHT);
    assert(player.lane == 1U);
    for (i = 1U; i < GAME_STEER_REPEAT_TICKS; i++)
    {
        Player_Update(&player, GAME_DIRECTION_RIGHT);
        assert(player.lane == 1U);
    }
    Player_Update(&player, GAME_DIRECTION_RIGHT);
    assert(player.lane == 2U);
    for (i = 0U; i < 1000U; i++)
    {
        Player_Update(&player, GAME_DIRECTION_RIGHT);
    }
    assert(player.lane == 2U);
    Player_Update(&player, GAME_DIRECTION_LEFT);
    Player_Update(&player, GAME_DIRECTION_CENTER);
    Player_Update(&player, GAME_DIRECTION_LEFT);
    assert(player.lane == 0U);
    Player_Update(&player, (GameDirection_t)99);
    assert(player.previous_direction == GAME_DIRECTION_CENTER);
}

static void TestCollisionAndRestart(void)
{
    Game_t game;
    Game_t saved;
    Player_t player;
    Obstacle_t obstacle;
    Player_Init(&player);
    Obstacle_Init(&obstacle);
    Obstacle_Spawn(&obstacle, player.lane);
    obstacle.y = GAME_PLAYER_Y - GAME_OBSTACLE_HEIGHT;
    assert(!Collision_HasHit(&player, &obstacle, obstacle.y));
    obstacle.y++;
    assert(Collision_HasHit(&player, &obstacle, obstacle.y));
    obstacle.y = GAME_PLAYER_Y + GAME_PLAYER_HEIGHT;
    assert(!Collision_HasHit(&player, &obstacle, obstacle.y));
    assert(Collision_HasHit(&player, &obstacle, 0));
    obstacle.lane = 0U;
    assert(!Collision_HasHit(&player, &obstacle, 0));
    obstacle.active = false;
    obstacle.lane = player.lane;
    assert(!Collision_HasHit(&player, &obstacle, 0));

    Start(&game);
    ClearObstacles(&game);
    Obstacle_Spawn(&game.obstacles[0], 0U);
    game.obstacles[0].y = GAME_WORLD_HEIGHT - 1;
    Obstacle_Spawn(&game.obstacles[1], game.player.lane);
    game.obstacles[1].y = GAME_PLAYER_Y - GAME_OBSTACLE_HEIGHT;
    assert(Tick(&game, GAME_DIRECTION_CENTER, false) ==
           (GAME_EVENT_COLLISION | GAME_EVENT_GAME_OVER));
    assert(game.score == 0U); /* collision beats a simultaneous pass */
    saved = game;
    assert(Tick(&game, GAME_DIRECTION_LEFT, false) == GAME_EVENT_NONE);
    AssertSame(&game, &saved);
    game.score = 20U;
    assert(Tick(&game, GAME_DIRECTION_LEFT, true) == GAME_EVENT_STARTED);
    assert(game.state == GAME_STATE_PLAYING);
    assert(game.score == 0U);
    assert(game.level == 1U);
    assert(game.player.lane == GAME_START_LANE);
    assert(game.obstacles[0].y == GAME_OBSTACLE_SPAWN_Y);
    assert(!game.obstacles[1].active);

    /* Steering into an overlapping obstacle must collide on this tick. */
    ClearObstacles(&game);
    Obstacle_Spawn(&game.obstacles[0], 0U);
    game.obstacles[0].y = GAME_PLAYER_Y;
    assert(Tick(&game, GAME_DIRECTION_LEFT, false) ==
           (GAME_EVENT_COLLISION | GAME_EVENT_GAME_OVER));
}

static void TestScoringAndDifficulty(void)
{
    Game_t game;
    uint32_t i;
    Start(&game);
    ClearObstacles(&game);
    for (i = 1U; i <= 40U; i++)
    {
        uint32_t expected_level = (i / GAME_SCORE_PER_LEVEL) + 1U;
        GameEvents_t events;
        Obstacle_Spawn(&game.obstacles[0], 0U);
        game.obstacles[0].y = GAME_WORLD_HEIGHT - 1;
        events = Tick(&game, GAME_DIRECTION_CENTER, false);
        assert((events & GAME_EVENT_SCORE_CHANGED) != 0U);
        assert(game.score == i);
        assert(!game.obstacles[0].active);
        if (expected_level > GAME_MAX_LEVEL)
        {
            expected_level = GAME_MAX_LEVEL;
        }
        assert(game.level == expected_level);
        assert(game.speed == (GAME_INITIAL_SPEED +
                             ((expected_level - 1U) * GAME_SPEED_PER_LEVEL)));
        assert(game.spawn_interval_ticks == (GAME_INITIAL_SPAWN_TICKS -
                   ((expected_level - 1U) * GAME_SPAWN_REDUCTION_TICKS)));
        assert(((events & GAME_EVENT_LEVEL_CHANGED) != 0U) ==
               (((i % GAME_SCORE_PER_LEVEL) == 0U) &&
                (i < (GAME_MAX_LEVEL * GAME_SCORE_PER_LEVEL))));
        assert(Tick(&game, GAME_DIRECTION_CENTER, false) == GAME_EVENT_NONE);
        assert(game.score == i); /* each obstacle scores once */
        ClearObstacles(&game);
    }
    game.score = UINT32_MAX;
    Obstacle_Spawn(&game.obstacles[0], 0U);
    game.obstacles[0].y = GAME_WORLD_HEIGHT - 1;
    assert(Tick(&game, GAME_DIRECTION_CENTER, false) == GAME_EVENT_NONE);
    assert(game.score == UINT32_MAX);
}

static void TestSpawnTimingAndFullPool(void)
{
    Game_t game;
    uint32_t i;
    uint32_t seed;
    Start(&game);
    for (i = 1U; i < GAME_INITIAL_SPAWN_TICKS; i++)
    {
        (void)Tick(&game, GAME_DIRECTION_CENTER, false);
        assert(!game.obstacles[1].active);
    }
    (void)Tick(&game, GAME_DIRECTION_CENTER, false);
    assert(game.obstacles[1].active);
    assert(game.obstacles[1].y == GAME_OBSTACLE_SPAWN_Y);
    for (i = 0U; i < GAME_MAX_OBSTACLES; i++)
    {
        Obstacle_Spawn(&game.obstacles[i], 0U);
    }
    seed = game.random_state;
    game.spawn_ticks = (uint16_t)(game.spawn_interval_ticks - 1U);
    (void)Tick(&game, GAME_DIRECTION_CENTER, false);
    assert(game.random_state == seed); /* no replacement or RNG consumption */
    for (i = 0U; i < GAME_MAX_OBSTACLES; i++)
    {
        assert(game.obstacles[i].y == (GAME_OBSTACLE_SPAWN_Y + game.speed));
    }
}

static void TestNaturalSurvival(void)
{
    Game_t game;
    uint32_t i;
    Start(&game);
    /* Check the destination too: blindly moving left can hit another car. */
    for (i = 0U; i < 12000U; i++)
    {
        uint32_t j;
        GameDirection_t direction = GAME_DIRECTION_CENTER;
        bool danger[GAME_LANE_COUNT] = {false, false, false};
        for (j = 0U; j < GAME_MAX_OBSTACLES; j++)
        {
            if (game.obstacles[j].active &&
                ((game.obstacles[j].y + (12 * game.speed) + GAME_OBSTACLE_HEIGHT)
                 > GAME_PLAYER_Y) &&
                (game.obstacles[j].y < (GAME_PLAYER_Y + GAME_PLAYER_HEIGHT)))
            {
                danger[game.obstacles[j].lane] = true;
            }
        }
        if (danger[game.player.lane])
        {
            if ((game.player.lane > 0U) && (!danger[game.player.lane - 1U]))
            {
                direction = GAME_DIRECTION_LEFT;
            }
            else if ((game.player.lane < (GAME_LANE_COUNT - 1U)) &&
                     (!danger[game.player.lane + 1U]))
            {
                direction = GAME_DIRECTION_RIGHT;
            }
            else
            {
                /* Stay until a neighboring lane opens. */
            }
        }
        (void)Tick(&game, direction, false);
        assert(game.state == GAME_STATE_PLAYING);
        assert(game.player.lane < GAME_LANE_COUNT);
    }
    assert(game.score > 100U);
    assert(game.level == GAME_MAX_LEVEL);
}

static void TestDeterminismAndLongRun(void)
{
    Game_t a;
    Game_t b;
    uint32_t i;
    Game_Init(&a, 9876U);
    Game_Init(&b, 9876U);
    for (i = 0U; i < 100000U; i++)
    {
        uint32_t j;
        const GameDirection_t direction = (GameDirection_t)((i / 19U) % 3U);
        const bool press = (a.state == GAME_STATE_MENU) ||
                           (a.state == GAME_STATE_GAME_OVER) ||
                           ((i % 101U) == 0U);
        assert(Tick(&a, direction, press) == Tick(&b, direction, press));
        AssertSame(&a, &b);
        assert(a.player.lane < GAME_LANE_COUNT);
        assert((a.level >= 1U) && (a.level <= GAME_MAX_LEVEL));
        for (j = 0U; j < GAME_MAX_OBSTACLES; j++)
        {
            assert(a.obstacles[j].lane < GAME_LANE_COUNT);
            assert(a.obstacles[j].y >= GAME_OBSTACLE_SPAWN_Y);
        }
    }
}

int main(void)
{
    TestStates();
    TestSteering();
    TestCollisionAndRestart();
    TestScoringAndDifficulty();
    TestSpawnTimingAndFullPool();
    TestNaturalSurvival();
    TestDeterminismAndLongRun();
    puts("PASS: 7 game test groups, 12,000 survival ticks, 100,000 replay ticks.");
    return 0;
}
