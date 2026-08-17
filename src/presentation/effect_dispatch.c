#include "presentation/effect_dispatch.h"

#include "core/game_effects.h"
#include "presentation/audio.h"
#include "presentation/particles.h"

static void EffectDispatchAudio(const GameAudioEffect *audio)
{
    switch (audio->cue) {
        case GAME_AUDIO_BREAK:
            AudioPlayBreak();
            break;
        case GAME_AUDIO_STEP:
            AudioPlayStep();
            break;
        case GAME_AUDIO_WATER_STEP:
            AudioPlayWaterStep();
            break;
        case GAME_AUDIO_SPLASH:
            AudioPlaySplash();
            break;
        case GAME_AUDIO_SET_RAIN:
            AudioSetRain(audio->enabled);
            break;
    }
}

void EffectDispatchPending(void)
{
    GameEffect effect;
    while (GameEffectsPoll(&effect)) {
        switch (effect.type) {
            case GAME_EFFECT_PARTICLE_ONE: {
                const GameParticleOneEffect *particle =
                    &effect.data.particleOne;
                ParticlesEmitOne(particle->position, particle->velocity,
                                 particle->color, particle->size,
                                 particle->life, particle->gravity);
                break;
            }
            case GAME_EFFECT_PARTICLE_BURST: {
                const GameParticleBurstEffect *particle =
                    &effect.data.particleBurst;
                ParticlesEmitBurst(particle->position, particle->color,
                                   particle->count, particle->speed,
                                   particle->life);
                break;
            }
            case GAME_EFFECT_PARTICLE_STYLED: {
                const GameParticleStyledEffect *particle =
                    &effect.data.particleStyled;
                ParticleStyle style = {
                    .startSize = particle->style.startSize,
                    .endSize = particle->style.endSize,
                    .startColor = particle->style.startColor,
                    .endColor = particle->style.endColor,
                    .gravity = particle->style.gravity
                };
                ParticlesEmitStyled(particle->position, particle->velocity,
                                    &style, particle->life);
                break;
            }
            case GAME_EFFECT_AUDIO:
                EffectDispatchAudio(&effect.data.audio);
                break;
        }
    }
}
