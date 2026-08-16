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
    Vector3 size;
    Color color;
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

void ParticlesEmitOne(Vector3 position, Vector3 velocity, Color color, Vector3 size, float life, float gravity)
{
    Particle *particle = &particles[NextFreeParticle()];
    particle->active = true;
    particle->position = position;
    particle->velocity = velocity;
    particle->color = color;
    particle->size = size;
    particle->life = life;
    particle->maxLife = life > 0.0f ? life : 1.0f;
    particle->gravity = gravity;
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

        float alpha = particle->life / particle->maxLife;
        Color color = particle->color;
        color.a = (unsigned char)((float)color.a * alpha);
        DrawCubeV(particle->position, particle->size, color);
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
