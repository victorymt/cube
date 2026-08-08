#ifndef VOXELCRAFT_SPACE_H
#define VOXELCRAFT_SPACE_H

#include "types.h"

#include <stdio.h>

#define MAX_SPACE_CHUNKS ((SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1) * (SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1))
#define MAX_SPACE_EDITS 65536

#define STAR_SYSTEM_SPACING 1400
#define STAR_SYSTEM_PROBABILITY 65u

typedef enum SpectrumType {
    SPECTRUM_RED_DWARF = 0,
    SPECTRUM_ORANGE,
    SPECTRUM_YELLOW,
    SPECTRUM_BLUE_WHITE,
    SPECTRUM_RED_GIANT
} SpectrumType;

typedef enum SolarBodyStyle {
    SOLAR_STYLE_SUN = 0,
    SOLAR_STYLE_LAVA,
    SOLAR_STYLE_ICE,
    SOLAR_STYLE_DESERT,
    SOLAR_STYLE_GAS,
    SOLAR_STYLE_CRATER
} SolarBodyStyle;

typedef struct SolarPlanetDef {
    int orbit;
    int size;
    int yOffset;
    SolarBodyStyle style;
} SolarPlanetDef;

typedef struct SolarSystemDef {
    bool exists;
    int anchorX;
    int anchorZ;
    Vector3 center;
    char name[32];
    SpectrumType spectrum;
    int starRadius;
    int planetCount;
    SolarPlanetDef planets[6];
} SolarSystemDef;

typedef struct SpaceBodyInfo {
    Vector3 center;
    float radius;
    float dist;
    bool isStar;
    int index;
    char name[32];
    SpectrumType spectrum;
    SolarBodyStyle style;
} SpaceBodyInfo;

typedef struct SpaceChunk {
    bool loaded;
    bool dirty;
    bool hasModel;
    bool hasWaterModel;
    int cx;
    int cz;
    bool hasStar;
    int starX;
    int starY;
    int starZ;
    Model model;
    Model waterModel;
    unsigned short blocks[CHUNK_SIZE][SPACE_LAYER_HEIGHT][CHUNK_SIZE];
} SpaceChunk;

extern SpaceChunk spaceChunks[MAX_SPACE_CHUNKS];

void SpaceInit(void);
void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame);
void SpaceUpdateStarGlow(Vector3 playerPosition);
void SpaceUpdateSolarGlow(Vector3 playerPosition);
BlockType SpaceBlockAt(int x, int y, int z);
void SpaceSetBlock(int x, int y, int z, BlockType type);
void SpaceSaveEdits(FILE *file);
void SpaceLoadEdits(FILE *file);
void UnloadAllSpaceChunks(void);
int GetActiveSpaceChunkCount(void);
int GetSpaceEditCount(void);
void SpaceRebuildTorchList(void);

bool StarSystemAt(int ax, int az, SolarSystemDef *out);
Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index);
int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out, int maxCount);
bool FindNearestSystem(Vector3 pos, float maxDist, SolarSystemDef *out, float *outDist);
int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out, int maxCount);
bool SpaceBodyPick(Vector3 origin, Vector3 direction, SpaceBodyInfo *out);
Color SpectrumColor(SpectrumType type);
const char *SpectrumName(SpectrumType type);
const char *SolarStyleName(SolarBodyStyle style);

#endif
