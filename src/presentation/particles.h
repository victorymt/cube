#ifndef VOXELCRAFT_PARTICLES_H
#define VOXELCRAFT_PARTICLES_H

#include "raylib.h"

#define PARTICLES_MAX 768

void ParticlesInit(void);
void ParticlesUpdate(float dt);
void ParticlesDraw(void);
void ParticlesClear(void);

void ParticlesEmitBurst(Vector3 position, Color color, int count, float speed, float life);
void ParticlesEmitOne(Vector3 position, Vector3 velocity, Color color, Vector3 size, float life, float gravity);
int ParticlesActiveCount(void);

#endif
