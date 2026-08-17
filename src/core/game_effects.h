#ifndef VOXELCRAFT_GAME_EFFECTS_H
#define VOXELCRAFT_GAME_EFFECTS_H

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define GAME_EFFECTS_CAPACITY 1024u

typedef enum GameEffectType {
    GAME_EFFECT_PARTICLE_ONE = 0,
    GAME_EFFECT_PARTICLE_BURST,
    GAME_EFFECT_PARTICLE_STYLED,
    GAME_EFFECT_AUDIO
} GameEffectType;

typedef enum GameAudioCue {
    GAME_AUDIO_BREAK = 0,
    GAME_AUDIO_STEP,
    GAME_AUDIO_WATER_STEP,
    GAME_AUDIO_SPLASH,
    GAME_AUDIO_SET_RAIN
} GameAudioCue;

typedef struct GameParticleStyle {
    Vector3 startSize;
    Vector3 endSize;
    Color startColor;
    Color endColor;
    float gravity;
} GameParticleStyle;

typedef struct GameParticleOneEffect {
    Vector3 position;
    Vector3 velocity;
    Color color;
    Vector3 size;
    float life;
    float gravity;
} GameParticleOneEffect;

typedef struct GameParticleBurstEffect {
    Vector3 position;
    Color color;
    int count;
    float speed;
    float life;
} GameParticleBurstEffect;

typedef struct GameParticleStyledEffect {
    Vector3 position;
    Vector3 velocity;
    GameParticleStyle style;
    float life;
} GameParticleStyledEffect;

typedef struct GameAudioEffect {
    GameAudioCue cue;
    bool enabled;
} GameAudioEffect;

typedef struct GameEffect {
    GameEffectType type;
    union {
        GameParticleOneEffect particleOne;
        GameParticleBurstEffect particleBurst;
        GameParticleStyledEffect particleStyled;
        GameAudioEffect audio;
    } data;
} GameEffect;

void GameEffectsReset(void);
bool GameEffectsPoll(GameEffect *out);
unsigned GameEffectsPendingCount(void);
uint64_t GameEffectsDroppedCount(void);

void GameEffectsEmitParticleOne(Vector3 position, Vector3 velocity,
                                Color color, Vector3 size, float life,
                                float gravity);
void GameEffectsEmitParticleBurst(Vector3 position, Color color, int count,
                                  float speed, float life);
void GameEffectsEmitParticleStyled(Vector3 position, Vector3 velocity,
                                   const GameParticleStyle *style,
                                   float life);
void GameEffectsPlayAudio(GameAudioCue cue);
void GameEffectsSetRain(bool enabled);

#endif
