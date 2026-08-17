#include "core/game_notice.h"

#include <stdio.h>

static char gameNotice[GAME_NOTICE_TEXT_CAPACITY] =
    "Flat mode: press I to import an image path.";
static float gameNoticeRemaining = 8.0f;

void GameNoticePost(const char *message)
{
    snprintf(gameNotice, sizeof(gameNotice), "%s", message ? message : "");
    gameNoticeRemaining = GAME_NOTICE_DURATION_SECONDS;
}

const char *GameNoticeCurrent(void)
{
    return gameNotice;
}

float GameNoticeRemaining(void)
{
    return gameNoticeRemaining;
}

void GameNoticeTick(float dt)
{
    if (gameNoticeRemaining > 0.0f) gameNoticeRemaining -= dt;
}
