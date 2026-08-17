#ifndef VOXELCRAFT_GAME_NOTICE_H
#define VOXELCRAFT_GAME_NOTICE_H

#define GAME_NOTICE_TEXT_CAPACITY 160
#define GAME_NOTICE_DURATION_SECONDS 6.0f

void GameNoticePost(const char *message);
const char *GameNoticeCurrent(void);
float GameNoticeRemaining(void);
void GameNoticeTick(float dt);

#endif
