#ifndef VOXELCRAFT_ECOLOGY_H
#define VOXELCRAFT_ECOLOGY_H

#include "types.h"

typedef enum PlanetFloraArchetype {
    PLANET_FLORA_ALIEN_CANOPY = 0,
    PLANET_FLORA_CRYSTAL,
    PLANET_FLORA_SPORE,
    PLANET_FLORA_THERMAL_VENT
} PlanetFloraArchetype;

typedef struct PlanetEcologyProfile {
    PlanetFloraArchetype flora;
    float floraDensity;
    float faunaDensity;
    float lifeDensity;
    BlockType primaryBlock;
    BlockType accentBlock;
} PlanetEcologyProfile;

PlanetEcologyProfile PlanetEcologyCurrent(void);
float PlanetEcologyFaunaDensity(void);
const char *PlanetEcologyLifeName(void);
void PlanetEcologyApplyToChunk(Chunk *chunk, int chunkX, int chunkZ);

#endif
