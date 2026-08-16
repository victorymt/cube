#include "presentation/particles.h"

#include "raymath.h"

#include <math.h>
#include <stdlib.h>

typedef struct Particle {
    bool active;
    Vector3 position;
    Vector3 velocity;
    float life;
    float maxLife;
    Vector3 startSize;
    Vector3 endSize;
    Color startColor;
    Color endColor;
    float gravity;
} Particle;

static Particle particles[PARTICLES_MAX];

static int NextFreeParticle(void)
{
    int start = rand() % PARTICLES_MAX;
    for (int i = 0; i < PARTICLES_MAX; i++) {
        int index = (start + i) % PARTICLES_MAX;
        if (!particles[index].active) return index;
    }
    return start;
}

void ParticlesInit(void)
{
    for (int i = 0; i < PARTICLES_MAX; i++) particles[i].active = false;
}

void ParticlesEmitStyled(Vector3 position, Vector3 velocity,
                         const ParticleStyle *style, float life)
{
    if (!style || !isfinite(life) || life <= 0.0f) return;
    Particle *particle = &particles[NextFreeParticle()];
    particle->active = true;
    particle->position = position;
    particle->velocity = velocity;
    particle->startColor = style->startColor;
    particle->endColor = style->endColor;
    particle->startSize = style->startSize;
    particle->endSize = style->endSize;
    particle->life = life;
    particle->maxLife = life;
    particle->gravity = style->gravity;
}

void ParticlesEmitOne(Vector3 position, Vector3 velocity, Color color,
                      Vector3 size, float life, float gravity)
{
    Color endColor = color;
    endColor.a = 0;
    ParticleStyle style = {
        .startSize = size,
        .endSize = size,
        .startColor = color,
        .endColor = endColor,
        .gravity = gravity
    };
    ParticlesEmitStyled(position, velocity, &style, life);
}

void ParticlesEmitBurst(Vector3 position, Color color, int count, float speed, float life)
{
    for (int i = 0; i < count; i++) {
        Vector3 velocity = {
            ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * speed,
            (float)rand() / (float)RAND_MAX * speed * 0.8f + speed * 0.3f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * speed
        };
        float size = 0.08f + (float)rand() / (float)RAND_MAX * 0.10f;
        Vector3 offset = {
            ((float)rand() / (float)RAND_MAX - 0.5f) * 0.7f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 0.7f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 0.7f
        };
        ParticlesEmitOne(Vector3Add(position, offset), velocity, color,
                         (Vector3){ size, size, size }, life * (0.6f + (float)rand() / (float)RAND_MAX * 0.8f), 12.0f);
    }
}

void ParticlesUpdate(float dt)
{
    for (int i = 0; i < PARTICLES_MAX; i++) {
        Particle *particle = &particles[i];
        if (!particle->active) continue;

        particle->velocity.y -= particle->gravity * dt;
        particle->position = Vector3Add(particle->position, Vector3Scale(particle->velocity, dt));
        particle->life -= dt;
        if (particle->life <= 0.0f) particle->active = false;
    }
}

void ParticlesDraw(void)
{
    for (int i = 0; i < PARTICLES_MAX; i++) {
        const Particle *particle = &particles[i];
        if (!particle->active) continue;

        float amount = 1.0f - particle->life / particle->maxLife;
        amount = Clamp(amount, 0.0f, 1.0f);
        Vector3 size = Vector3Lerp(particle->startSize,
                                   particle->endSize, amount);
        Color color = ColorLerp(particle->startColor,
                                particle->endColor, amount);
        DrawCubeV(particle->position, size, color);
    }
}

void ParticlesClear(void)
{
    for (int i = 0; i < PARTICLES_MAX; i++) particles[i].active = false;
}

int ParticlesActiveCount(void)
{
    int count = 0;
    for (int i = 0; i < PARTICLES_MAX; i++) {
        if (particles[i].active) count++;
    }
    return count;
}
