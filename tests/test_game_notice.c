#include "core/game_notice.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void TestInitialNotice(void)
{
    assert(strcmp(GameNoticeCurrent(),
                  "Flat mode: press I to import an image path.") == 0);
    assert(GameNoticeRemaining() == 8.0f);
}

static void TestPostAndTick(void)
{
    GameNoticePost("Saved map");
    assert(strcmp(GameNoticeCurrent(), "Saved map") == 0);
    assert(GameNoticeRemaining() == GAME_NOTICE_DURATION_SECONDS);

    GameNoticeTick(1.25f);
    assert(GameNoticeRemaining() == 4.75f);
    GameNoticeTick(10.0f);
    assert(GameNoticeRemaining() <= 0.0f);
    float expired = GameNoticeRemaining();
    GameNoticeTick(1.0f);
    assert(GameNoticeRemaining() == expired);
}

static void TestInputBounds(void)
{
    char longMessage[GAME_NOTICE_TEXT_CAPACITY * 2];
    memset(longMessage, 'x', sizeof(longMessage));
    longMessage[sizeof(longMessage) - 1] = '\0';
    GameNoticePost(longMessage);
    assert(strlen(GameNoticeCurrent()) == GAME_NOTICE_TEXT_CAPACITY - 1u);

    GameNoticePost(NULL);
    assert(GameNoticeCurrent()[0] == '\0');
    assert(GameNoticeRemaining() == GAME_NOTICE_DURATION_SECONDS);
}

int main(void)
{
    TestInitialNotice();
    TestPostAndTick();
    TestInputBounds();
    puts("game notice tests passed");
    return 0;
}
