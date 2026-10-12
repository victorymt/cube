#ifndef VOXELCRAFT_CGA_VOXEL_H
#define VOXELCRAFT_CGA_VOXEL_H

#include "world/world_types.h"

#include <stddef.h>

#define CGA_MAX_FEATURES_PER_CHUNK 16
#define CGA_MAX_CHUNK_OPS 2048
#define CGA_HOUSE_GRAMMAR_VERSION 5u
#define CGA_MINESHAFT_GRAMMAR_VERSION 2u
#define CGA_DUNGEON_GRAMMAR_VERSION 3u
#define CGA_DESERT_TEMPLE_GRAMMAR_VERSION 4u

typedef enum CgaAxis {
    CGA_AXIS_X = 0,
    CGA_AXIS_Y,
    CGA_AXIS_Z
} CgaAxis;

typedef enum CgaOrientation {
    CGA_ORIENTATION_0 = 0,
    CGA_ORIENTATION_90,
    CGA_ORIENTATION_180,
    CGA_ORIENTATION_270
} CgaOrientation;

/* World AABB in half-open integer coordinates; orientation defines the local frame.
 * Split/repeat axes refer to the world axes. */
typedef struct CgaScope {
    int minX;
    int minY;
    int minZ;
    int sizeX;
    int sizeY;
    int sizeZ;
    CgaOrientation orientation;
} CgaScope;

typedef enum CgaOpKind {
    CGA_OP_FILL = 0,
    CGA_OP_CARVE,
    CGA_OP_PAINT
} CgaOpKind;

typedef enum CgaStage {
    CGA_STAGE_BASE = 0,
    CGA_STAGE_CARVE,
    CGA_STAGE_PAINT
} CgaStage;

typedef struct CgaFeature {
    uint64_t id;
    uint32_t grammarVersion;
    uint32_t seed;
    int anchorX;
    int anchorZ;
    CgaScope scope;
} CgaFeature;

typedef struct CgaVoxelOp {
    CgaOpKind kind;
    CgaStage stage;
    int priority;
    uint64_t featureId;
    uint32_t sequence;
    CgaScope scope;
    BlockType material;
} CgaVoxelOp;

typedef struct CgaWorkspace {
    CgaFeature features[CGA_MAX_FEATURES_PER_CHUNK];
    size_t featureCount;
    CgaVoxelOp ops[CGA_MAX_CHUNK_OPS];
    size_t opCount;
    bool overflow;
} CgaWorkspace;

/* The callback reads an immutable planning or base-terrain snapshot.  A NULL
 * context or callback means that no external occupancy snapshot is available. */
typedef struct CgaContext {
    void *userData;
    bool (*regionClear)(void *userData, CgaScope region, uint64_t excludedFeature);
} CgaContext;

void CgaWorkspaceReset(CgaWorkspace *workspace);
bool CgaScopeValid(CgaScope scope);
bool CgaScopeSplit(CgaScope parent, CgaAxis axis, const int *sizes,
                   size_t count, CgaScope *out);
size_t CgaScopeRepeat(CgaScope parent, CgaAxis axis, int requestedSize,
                      CgaScope *out, size_t capacity);
uint64_t CgaFeatureId(uint32_t worldSeed, uint32_t grammarVersion,
                      int anchorX, int anchorZ);

/* Fixed-capacity stages; reset the workspace before planning. Expansion only emits IR.
 * Evaluation sorts by (stage, priority, feature ID, sequence), later writes win.
 * Paint applies its material after the carve stage. The caller owns the target chunk. */
bool CgaPlanHouseFeatures(CgaWorkspace *workspace, int cx, int cz,
                          TerrainMode mode, const CgaContext *context);
bool CgaPlanStructureFeatures(CgaWorkspace *workspace, int cx, int cz,
                              TerrainMode mode, const CgaContext *context);
bool CgaExpandHouse(CgaWorkspace *workspace, CgaFeature feature);
bool CgaExpandFeature(CgaWorkspace *workspace, CgaFeature feature);
bool CgaAppendOp(CgaWorkspace *workspace, CgaOpKind kind, CgaStage stage,
                 int priority, CgaFeature feature, CgaScope scope, BlockType material);
bool CgaEvaluateChunk(Chunk *chunk, int cx, int cz, CgaWorkspace *workspace);

/* Generates the first CGA-backed structure set for a Homeworld chunk. */
bool CgaGenerateHouseFeatures(Chunk *chunk, int cx, int cz, TerrainMode mode,
                              const CgaContext *context);
bool CgaGenerateStructureFeatures(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode, const CgaContext *context);

#ifdef TERRAIN_TESTING
bool CgaGenerateMineshaftFeatures(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode,
                                  const CgaContext *context);
#endif

#ifdef TERRAIN_TESTING
size_t CgaTestPlanStructureFeatures(CgaFeature *out, size_t capacity,
                                    int cx, int cz, TerrainMode mode,
                                    const CgaContext *context);
size_t CgaTestPlanHouseFeatures(CgaFeature *out, size_t capacity,
                                int cx, int cz, TerrainMode mode);
size_t CgaTestPlanHouseFeaturesWithContext(CgaFeature *out, size_t capacity,
                                           int cx, int cz, TerrainMode mode,
                                           const CgaContext *context);
size_t CgaTestResolveFeatureConflicts(CgaFeature *features, size_t count,
                                      const CgaContext *context);
bool CgaTestExpandHouse(CgaWorkspace *workspace, CgaFeature feature);
bool CgaTestEvaluateWorkspace(Chunk *chunk, int cx, int cz,
                              CgaWorkspace *workspace);
#endif

#endif
