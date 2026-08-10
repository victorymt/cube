#ifndef VOXELCRAFT_ECOLOGY_TEST_FIXTURE_H
#define VOXELCRAFT_ECOLOGY_TEST_FIXTURE_H

#include "chunks.h"
#include "space.h"
#include "world.h"

#include <stdint.h>
#include <stdio.h>

void EcologyTestSetSeed(uint32_t seed);
void EcologyTestClearBlockEdits(void);
void EcologyTestAddBlockEdit(int x, int y, int z, BlockType type);
void EcologyTestSetBlockEditType(int index, BlockType type);
int EcologyTestBlockEditCount(void);
uint64_t EcologyTestBlockEditRevision(void);
int EcologyTestBlockEditReadCount(void);
void EcologyTestResetBlockEditReadCount(void);

void EcologyTestActivatePlanetStyle(
    uint32_t seed, int originX, int originZ, SolarBodyStyle style);
void EcologyTestActivatePlanetStyleWithFile(
    FILE *file, uint32_t seed, int originX, int originZ,
    SolarBodyStyle style);
void EcologyTestActivatePlanet(uint32_t seed, int originX, int originZ);
void EcologyTestSaveSimulationState(FILE *file);
void EcologyTestLoadSimulationState(FILE *file);

#endif
