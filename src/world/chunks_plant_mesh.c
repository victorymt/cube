#include "world/chunks_internal.h"

#include "ecology/flora_taxa.h"

static void AddPlantFace(ChunkMeshEmitter *emitter, Vector3 corners[6],
                         Vector3 normal, Vector2 uvs[6], Color color)
{
    AddMeshFaceLighting(emitter, corners, normal, uvs, color, NULL, 0.0f);
}

static void PlantDimensions(BlockType type, float *height, float *halfWidth)
{
    *height = 0.4f;
    *halfWidth = 0.16f;
    if (type == BLOCK_FLOWER) {
        *height = 0.62f;
        *halfWidth = 0.22f;
    } else if (type == BLOCK_MUSHROOM) {
        *height = 0.36f;
        *halfWidth = 0.25f;
    } else if (type == BLOCK_TALL_GRASS) {
        *height = 0.78f;
        *halfWidth = 0.25f;
    } else if (type == BLOCK_FERN) {
        *height = 0.62f;
        *halfWidth = 0.28f;
    } else if (type == BLOCK_REED) {
        *height = 0.96f;
        *halfWidth = 0.20f;
    } else if (type == BLOCK_LICHEN) {
        *height = 0.34f;
        *halfWidth = 0.28f;
    } else {
        FloraTaxonVisualDimensions(type, height, halfWidth);
    }
}

static void AddPlantCarpet(ChunkMeshEmitter *emitter, int x, int y, int z,
                           Vector2 uvs[6], float brightness)
{
    float inset = 0.035f;
    float top = (float)y + 0.035f;
    Vector3 topFace[6] = {
        { (float)x + inset, top, (float)z + inset },
        { (float)x + 1.0f - inset, top, (float)z + inset },
        { (float)x + 1.0f - inset, top, (float)z + 1.0f - inset },
        { (float)x + inset, top, (float)z + inset },
        { (float)x + 1.0f - inset, top, (float)z + 1.0f - inset },
        { (float)x + inset, top, (float)z + 1.0f - inset }
    };
    Vector3 bottomFace[6] = {
        topFace[5], topFace[4], topFace[3],
        topFace[2], topFace[1], topFace[0]
    };
    AddPlantFace(emitter, topFace, (Vector3){ 0.0f, 1.0f, 0.0f }, uvs,
                 ShadeColor(WHITE, brightness));
    AddPlantFace(emitter, bottomFace, (Vector3){ 0.0f, -1.0f, 0.0f }, uvs,
                 ShadeColor(WHITE, 0.72f * brightness));
}

void AddPlantMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                  BlockType type, float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TextureForBlockFace(type, 0), uvs);

    if (BlockRenderShapeFor(type) == BLOCK_RENDER_CARPET) {
        AddPlantCarpet(emitter, x, y, z, uvs, brightness);
        return;
    }

    float plantHeight = 0.0f;
    float halfWidth = 0.0f;
    PlantDimensions(type, &plantHeight, &halfWidth);
    float y1 = y0 + plantHeight;

    unsigned int rotationHash = (unsigned int)x * 0x9e3779b1u;
    rotationHash ^= (unsigned int)y * 0x85ebca6bu;
    rotationHash ^= (unsigned int)z * 0xc2b2ae35u;
    rotationHash ^= (unsigned int)type * 0x27d4eb2du;
    float baseAngle = (float)(rotationHash & 1023u) *
                      (6.28318530718f / 1024.0f);
    for (int plane = 0; plane < 3; plane++) {
        float angle = baseAngle + (float)plane * 1.04719755120f;
        float dx = cosf(angle) * halfWidth;
        float dz = sinf(angle) * halfWidth;
        Vector3 quad[6] = {
            { cx - dx, y0, cz - dz },
            { cx + dx, y0, cz + dz },
            { cx + dx, y1, cz + dz },
            { cx - dx, y0, cz - dz },
            { cx + dx, y1, cz + dz },
            { cx - dx, y1, cz - dz }
        };
        Vector3 normal = Vector3Normalize((Vector3){ -dz, 0.0f, dx });
        float planeLight = 0.88f + (float)plane * 0.045f;
        AddPlantFace(emitter, quad, normal, uvs,
                     ShadeColor(WHITE, planeLight * brightness));
    }
}
