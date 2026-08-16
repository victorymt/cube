#ifndef VOXELCRAFT_PARTICLES_H
#define VOXELCRAFT_PARTICLES_H

#include "raylib.h"

#define PARTICLES_MAX 768

typedef struct ParticleStyle {
    Vector3 startSize;
    Vector3 endSize;
    Color startColor;
    Color endColor;
    float gravity;
} ParticleStyle;

void ParticlesInit(void);
void ParticlesUpdate(float dt);
void ParticlesDraw(void);
void ParticlesClear(void);

void ParticlesEmitStyled(Vector3 position, Vector3 velocity,
                         const ParticleStyle *style, float life);
void ParticlesEmitBurst(Vector3 position, Color color, int count, float speed, float life);
void ParticlesEmitOne(Vector3 position, Vector3 velocity, Color color, Vector3 size, float life, float gravity);
int ParticlesActiveCount(void);

#endif
