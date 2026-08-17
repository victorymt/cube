#include "core/game_effects.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void TestEffectPayloadsAndOrder(void)
{
    GameEffectsReset();
    assert(GameEffectsPendingCount() == 0u);
    assert(GameEffectsDroppedCount() == 0u);

    GameParticleStyle style = {
        .startSize = { 1.0f, 2.0f, 3.0f },
        .endSize = { 4.0f, 5.0f, 6.0f },
        .startColor = { 10, 20, 30, 40 },
        .endColor = { 50, 60, 70, 80 },
        .gravity = 9.0f
    };
    GameEffectsPlayAudio(GAME_AUDIO_STEP);
    GameEffectsSetRain(true);
    GameEffectsEmitParticleOne(
        (Vector3){ 1.0f, 2.0f, 3.0f },
        (Vector3){ 4.0f, 5.0f, 6.0f },
        (Color){ 7, 8, 9, 10 },
        (Vector3){ 11.0f, 12.0f, 13.0f }, 14.0f, 15.0f);
    GameEffectsEmitParticleStyled(
        (Vector3){ 16.0f, 17.0f, 18.0f },
        (Vector3){ 19.0f, 20.0f, 21.0f }, &style, 22.0f);
    GameEffectsEmitParticleBurst(
        (Vector3){ 23.0f, 24.0f, 25.0f },
        (Color){ 26, 27, 28, 29 }, 30, 31.0f, 32.0f);
    GameEffectsEmitParticleStyled(
        (Vector3){ 0 }, (Vector3){ 0 }, NULL, 1.0f);
    assert(GameEffectsPendingCount() == 5u);

    GameEffect effect = { 0 };
    assert(!GameEffectsPoll(NULL));
    assert(GameEffectsPendingCount() == 5u);

    assert(GameEffectsPoll(&effect));
    assert(effect.type == GAME_EFFECT_AUDIO);
    assert(effect.data.audio.cue == GAME_AUDIO_STEP);
    assert(!effect.data.audio.enabled);

    assert(GameEffectsPoll(&effect));
    assert(effect.type == GAME_EFFECT_AUDIO);
    assert(effect.data.audio.cue == GAME_AUDIO_SET_RAIN);
    assert(effect.data.audio.enabled);

    assert(GameEffectsPoll(&effect));
    assert(effect.type == GAME_EFFECT_PARTICLE_ONE);
    assert(effect.data.particleOne.position.x == 1.0f);
    assert(effect.data.particleOne.velocity.z == 6.0f);
    assert(effect.data.particleOne.color.a == 10);
    assert(effect.data.particleOne.size.y == 12.0f);
    assert(effect.data.particleOne.life == 14.0f);
    assert(effect.data.particleOne.gravity == 15.0f);

    assert(GameEffectsPoll(&effect));
    assert(effect.type == GAME_EFFECT_PARTICLE_STYLED);
    assert(effect.data.particleStyled.position.y == 17.0f);
    assert(effect.data.particleStyled.velocity.x == 19.0f);
    assert(effect.data.particleStyled.style.endSize.z == 6.0f);
    assert(effect.data.particleStyled.style.startColor.g == 20);
    assert(effect.data.particleStyled.style.gravity == 9.0f);
    assert(effect.data.particleStyled.life == 22.0f);

    assert(GameEffectsPoll(&effect));
    assert(effect.type == GAME_EFFECT_PARTICLE_BURST);
    assert(effect.data.particleBurst.position.z == 25.0f);
    assert(effect.data.particleBurst.color.r == 26);
    assert(effect.data.particleBurst.count == 30);
    assert(effect.data.particleBurst.speed == 31.0f);
    assert(effect.data.particleBurst.life == 32.0f);
    assert(!GameEffectsPoll(&effect));
}

static void TestCapacityOverflowAndWraparound(void)
{
    GameEffectsReset();
    for (unsigned index = 0u; index < GAME_EFFECTS_CAPACITY; index++) {
        GameEffectsEmitParticleBurst(
            (Vector3){ 0 }, (Color){ 0 }, (int)index, 0.0f, 0.0f);
    }
    GameEffectsPlayAudio(GAME_AUDIO_BREAK);
    assert(GameEffectsPendingCount() == GAME_EFFECTS_CAPACITY);
    assert(GameEffectsDroppedCount() == 1u);

    GameEffect effect = { 0 };
    unsigned half = GAME_EFFECTS_CAPACITY / 2u;
    for (unsigned index = 0u; index < half; index++) {
        assert(GameEffectsPoll(&effect));
        assert(effect.type == GAME_EFFECT_PARTICLE_BURST);
        assert(effect.data.particleBurst.count == (int)index);
    }
    for (unsigned index = 0u; index < half; index++) {
        GameEffectsEmitParticleBurst(
            (Vector3){ 0 }, (Color){ 0 },
            (int)(GAME_EFFECTS_CAPACITY + index), 0.0f, 0.0f);
    }
    assert(GameEffectsPendingCount() == GAME_EFFECTS_CAPACITY);

    for (unsigned index = half; index < GAME_EFFECTS_CAPACITY; index++) {
        assert(GameEffectsPoll(&effect));
        assert(effect.data.particleBurst.count == (int)index);
    }
    for (unsigned index = 0u; index < half; index++) {
        assert(GameEffectsPoll(&effect));
        assert(effect.data.particleBurst.count ==
               (int)(GAME_EFFECTS_CAPACITY + index));
    }
    assert(!GameEffectsPoll(&effect));

    GameEffectsReset();
    assert(GameEffectsPendingCount() == 0u);
    assert(GameEffectsDroppedCount() == 0u);
}

int main(void)
{
    TestEffectPayloadsAndOrder();
    TestCapacityOverflowAndWraparound();
    puts("game effects tests passed");
    return 0;
}
