#include "core/game_effects.h"

static GameEffect effects[GAME_EFFECTS_CAPACITY];
static unsigned effectHead = 0u;
static unsigned effectCount = 0u;
static uint64_t droppedEffectCount = 0u;

static void GameEffectsPush(GameEffect effect)
{
    if (effectCount >= GAME_EFFECTS_CAPACITY) {
        droppedEffectCount++;
        return;
    }
    unsigned index = (effectHead + effectCount) % GAME_EFFECTS_CAPACITY;
    effects[index] = effect;
    effectCount++;
}

void GameEffectsReset(void)
{
    effectHead = 0u;
    effectCount = 0u;
    droppedEffectCount = 0u;
}

bool GameEffectsPoll(GameEffect *out)
{
    if (!out || effectCount == 0u) return false;
    *out = effects[effectHead];
    effectHead = (effectHead + 1u) % GAME_EFFECTS_CAPACITY;
    effectCount--;
    return true;
}

unsigned GameEffectsPendingCount(void)
{
    return effectCount;
}

uint64_t GameEffectsDroppedCount(void)
{
    return droppedEffectCount;
}

void GameEffectsEmitParticleOne(Vector3 position, Vector3 velocity,
                                Color color, Vector3 size, float life,
                                float gravity)
{
    GameEffectsPush((GameEffect){
        .type = GAME_EFFECT_PARTICLE_ONE,
        .data.particleOne = {
            .position = position,
            .velocity = velocity,
            .color = color,
            .size = size,
            .life = life,
            .gravity = gravity
        }
    });
}

void GameEffectsEmitParticleBurst(Vector3 position, Color color, int count,
                                  float speed, float life)
{
    GameEffectsPush((GameEffect){
        .type = GAME_EFFECT_PARTICLE_BURST,
        .data.particleBurst = {
            .position = position,
            .color = color,
            .count = count,
            .speed = speed,
            .life = life
        }
    });
}

void GameEffectsEmitParticleStyled(Vector3 position, Vector3 velocity,
                                   const GameParticleStyle *style,
                                   float life)
{
    if (!style) return;
    GameEffectsPush((GameEffect){
        .type = GAME_EFFECT_PARTICLE_STYLED,
        .data.particleStyled = {
            .position = position,
            .velocity = velocity,
            .style = *style,
            .life = life
        }
    });
}

void GameEffectsPlayAudio(GameAudioCue cue)
{
    GameEffectsPush((GameEffect){
        .type = GAME_EFFECT_AUDIO,
        .data.audio = { .cue = cue, .enabled = false }
    });
}

void GameEffectsSetRain(bool enabled)
{
    GameEffectsPush((GameEffect){
        .type = GAME_EFFECT_AUDIO,
        .data.audio = { .cue = GAME_AUDIO_SET_RAIN, .enabled = enabled }
    });
}
