#include "world/chunks_internal.h"

bool BuildFloraMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, false, false, true, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

static bool FloraStructureExpectedBlockAt(
    const FloraStructureInstance *structure,
    int worldX, int y, int worldZ, BlockType *outBlock)
{
    if (worldX < structure->minX || worldX > structure->maxX ||
        y < structure->minY || y > structure->maxY ||
        worldZ < structure->minZ || worldZ > structure->maxZ) return false;

    int base = structure->groundY + 1;
    int offsetX = worldX - structure->rootX;
    int offsetZ = worldZ - structure->rootZ;
    switch (structure->kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY: {
        int trunkHeight = 3 + (int)(structure->shapeHash % 3u);
        int distance = abs(offsetX) + abs(offsetZ);
        if (offsetX == 0 && offsetZ == 0 &&
            y == base + trunkHeight + 1) {
            *outBlock = BLOCK_LUMINOUS_POD;
            return true;
        }
        if ((y == base + trunkHeight - 1 && distance <= 3) ||
            (y == base + trunkHeight && distance < 2)) {
            *outBlock = structure->accentBlock;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + trunkHeight) {
            *outBlock = structure->primaryBlock;
            return true;
        }
    } break;
    case FLORA_STRUCTURE_CRYSTAL: {
        int height = 2 + (int)(structure->shapeHash % 4u);
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + height) {
            *outBlock = y == base + height - 1 ?
                        structure->accentBlock : structure->primaryBlock;
            return true;
        }
        bool branch = structure->shapeHash & 1u ?
            ((offsetX == -1 && offsetZ == 0 && y == base + 1) ||
             (offsetX == 1 && offsetZ == 0 && y == base)) :
            ((offsetX == 0 && offsetZ == -1 && y == base + 1) ||
             (offsetX == 0 && offsetZ == 1 && y == base));
        if (branch) {
            *outBlock = structure->accentBlock;
            return true;
        }
    } break;
    case FLORA_STRUCTURE_SPORE: {
        int stemHeight = 2 + (int)(structure->shapeHash % 2u);
        int distance = abs(offsetX) + abs(offsetZ);
        if (offsetX == 0 && offsetZ == 0 &&
            y == base + stemHeight + 1) {
            *outBlock = BLOCK_LUMINOUS_POD;
            return true;
        }
        if (y == base + stemHeight && distance <= 1) {
            *outBlock = structure->accentBlock;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + stemHeight) {
            *outBlock = structure->primaryBlock;
            return true;
        }
    } break;
    case FLORA_STRUCTURE_THERMAL_VENT: {
        int height = 2 + (int)(structure->shapeHash % 3u);
        if (offsetX == 0 && offsetZ == 1 && y == base + height) {
            *outBlock = structure->accentBlock;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 && y == base + height) {
            *outBlock = BLOCK_LUMINOUS_POD;
            return true;
        }
        if (abs(offsetX) == 1 && offsetZ == 0 && y == base) {
            *outBlock = BLOCK_SCORIA;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + height) {
            *outBlock = structure->primaryBlock;
            return true;
        }
    } break;
    }
    return false;
}

static int FloraStructureOwnerAt(
    const FloraStructureInstance *structures, int structureCount,
    int worldX, int y, int worldZ, BlockType actualBlock)
{
    for (int index = structureCount - 1; index >= 0; index--) {
        BlockType expectedBlock = BLOCK_AIR;
        if (FloraStructureExpectedBlockAt(
                &structures[index], worldX, y, worldZ, &expectedBlock) &&
            actualBlock == expectedBlock) return index;
    }
    return -1;
}

static void CopyBlocksWithoutFloraStructures(
    unsigned short destination[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned short source[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount)
{
    memcpy(destination, source,
           sizeof(unsigned short) * CHUNK_SIZE * SURFACE_SECTION_HEIGHT * CHUNK_SIZE);
    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                if (FloraStructureOwnerAt(
                        structures, structureCount,
                        startX + lx, layerY + y, startZ + lz,
                        (BlockType)source[lx][y][lz]) >= 0) {
                    destination[lx][y][lz] = (unsigned short)BLOCK_AIR;
                }
            }
        }
    }
}

bool MergeMeshData(Mesh *target, Mesh *source)
{
    if (source->vertexCount <= 0) {
        FreeMeshData(source);
        return true;
    }
    if (target->vertexCount <= 0) {
        *target = *source;
        *source = (Mesh){ 0 };
        return true;
    }

    int targetVertexCount = target->vertexCount;
    int sourceVertexCount = source->vertexCount;
    int vertexCount = targetVertexCount + sourceVertexCount;
    float *vertices = malloc((size_t)vertexCount * 3u * sizeof(float));
    float *texcoords = malloc((size_t)vertexCount * 2u * sizeof(float));
    float *texcoords2 = malloc((size_t)vertexCount * 2u * sizeof(float));
    float *normals = malloc((size_t)vertexCount * 3u * sizeof(float));
    unsigned char *colors = malloc((size_t)vertexCount * 4u);
    if (!vertices || !texcoords || !texcoords2 || !normals || !colors) {
        free(vertices);
        free(texcoords);
        free(texcoords2);
        free(normals);
        free(colors);
        FreeMeshData(source);
        return false;
    }

    memcpy(vertices, target->vertices,
           (size_t)targetVertexCount * 3u * sizeof(float));
    memcpy(vertices + targetVertexCount * 3, source->vertices,
           (size_t)sourceVertexCount * 3u * sizeof(float));
    memcpy(texcoords, target->texcoords,
           (size_t)targetVertexCount * 2u * sizeof(float));
    memcpy(texcoords + targetVertexCount * 2, source->texcoords,
           (size_t)sourceVertexCount * 2u * sizeof(float));
    memcpy(texcoords2, target->texcoords2,
           (size_t)targetVertexCount * 2u * sizeof(float));
    memcpy(texcoords2 + targetVertexCount * 2, source->texcoords2,
           (size_t)sourceVertexCount * 2u * sizeof(float));
    memcpy(normals, target->normals,
           (size_t)targetVertexCount * 3u * sizeof(float));
    memcpy(normals + targetVertexCount * 3, source->normals,
           (size_t)sourceVertexCount * 3u * sizeof(float));
    memcpy(colors, target->colors, (size_t)targetVertexCount * 4u);
    memcpy(colors + targetVertexCount * 4, source->colors,
           (size_t)sourceVertexCount * 4u);

    int triangleCount = target->triangleCount + source->triangleCount;
    FreeMeshData(target);
    FreeMeshData(source);
    *target = (Mesh){
        .vertexCount = vertexCount,
        .triangleCount = triangleCount,
        .vertices = vertices,
        .texcoords = texcoords,
        .texcoords2 = texcoords2,
        .normals = normals,
        .colors = colors
    };
    return true;
}

static bool AppendFloraVisualInstance(
    FloraVisualInstance **instances, int *instanceCount,
    FloraVisualInstance instance)
{
    FloraVisualInstance *resized = realloc(
        *instances,
        (size_t)(*instanceCount + 1) * sizeof(FloraVisualInstance));
    if (!resized) return false;
    resized[*instanceCount] = instance;
    *instances = resized;
    (*instanceCount)++;
    return true;
}

static bool AppendPlantMeshInstances(
    FloraVisualInstance **instances, int *instanceCount,
    const Mesh *mesh, int firstVertexOffset)
{
    for (int firstVertex = 0; firstVertex < mesh->vertexCount;
         firstVertex += 12) {
        int vertexCount = mesh->vertexCount - firstVertex;
        if (vertexCount > 12) vertexCount = 12;
        float groundY = INFINITY;
        float topY = -INFINITY;
        for (int vertex = firstVertex;
             vertex < firstVertex + vertexCount; vertex++) {
            groundY = fminf(groundY, mesh->vertices[vertex * 3 + 1]);
            topY = fmaxf(topY, mesh->vertices[vertex * 3 + 1]);
        }
        float firstX = mesh->vertices[firstVertex * 3];
        float firstZ = mesh->vertices[firstVertex * 3 + 2];
        if (!AppendFloraVisualInstance(
                instances, instanceCount, (FloraVisualInstance){
                    .firstVertex = firstVertexOffset + firstVertex,
                    .vertexCount = vertexCount,
                    .anchor = {
                        floorf(firstX) + 0.5f,
                        groundY,
                        floorf(firstZ) + 0.5f
                    },
                    .height = topY - groundY,
                    .windResponse = topY - groundY < 0.10f ? 0.05f : 1.0f
                })) {
            return false;
        }
    }
    return true;
}

bool BuildChunkSurfaceSolidMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh)
{
    unsigned short solidBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    CopyBlocksWithoutFloraStructures(
        solidBlocks, blocks, layerY, chunkX, chunkZ, structures, structureCount);
    return BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])solidBlocks,
        SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
        nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildChunkSurfaceWaterMeshDataWithSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char *waterVolumes, int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, const SurfaceWaterBoundarySnapshot *boundary,
    Mesh *outMesh)
{
    unsigned short waterBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    CopyBlocksWithoutFloraStructures(
        waterBlocks, blocks, layerY, chunkX, chunkZ, structures, structureCount);
    return BuildSurfaceWaterMeshDataWithSnapshot(
        (const unsigned short (*)[CHUNK_SIZE])waterBlocks,
        waterVolumes, SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ,
        faces, nearbyTorchIndices, nearbyTorchCount, boundary, outMesh);
}

bool BuildChunkFloraMeshDataFromSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    *outMesh = (Mesh){ 0 };
    *outInstances = NULL;
    *outInstanceCount = 0;

    unsigned short floraBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    CopyBlocksWithoutFloraStructures(
        floraBlocks, blocks, layerY, chunkX, chunkZ, structures, structureCount);

    Mesh combined = { 0 };
    FloraVisualInstance *instances = NULL;
    int instanceCount = 0;
    Mesh plants = { 0 };
    if (BuildFloraMeshData(
            (const unsigned short (*)[CHUNK_SIZE])floraBlocks,
            SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
            nearbyTorchIndices, nearbyTorchCount, &plants)) {
        if (!AppendPlantMeshInstances(
                &instances, &instanceCount, &plants, 0) ||
            !MergeMeshData(&combined, &plants)) {
            FreeMeshData(&plants);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
    }

    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;
    for (int structureIndex = 0; structureIndex < structureCount;
         structureIndex++) {
        unsigned short instanceBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE] = { 0 };
        bool hasBlocks = false;
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    unsigned short block = blocks[lx][y][lz];
                    if (FloraStructureOwnerAt(
                            structures, structureCount,
                            startX + lx, layerY + y, startZ + lz,
                            (BlockType)block) != structureIndex) continue;
                    if (block == (unsigned short)BLOCK_AIR) continue;
                    instanceBlocks[lx][y][lz] = block;
                    hasBlocks = true;
                }
            }
        }
        if (!hasBlocks) continue;

        Mesh instanceMesh = { 0 };
        Mesh solid = { 0 };
        Mesh transparent = { 0 };
        Mesh crossed = { 0 };
        if (BuildSurfaceSolidMeshData(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
                nearbyTorchIndices, nearbyTorchCount, &solid) &&
            !MergeMeshData(&instanceMesh, &solid)) {
            FreeMeshData(&solid);
            FreeMeshData(&instanceMesh);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
        if (BuildMeshDataFiltered(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ,
                true, false, false, true, faces,
                nearbyTorchIndices, nearbyTorchCount, &transparent) &&
            !MergeMeshData(&instanceMesh, &transparent)) {
            FreeMeshData(&transparent);
            FreeMeshData(&instanceMesh);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
        if (BuildFloraMeshData(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
                nearbyTorchIndices, nearbyTorchCount, &crossed) &&
            !MergeMeshData(&instanceMesh, &crossed)) {
            FreeMeshData(&crossed);
            FreeMeshData(&instanceMesh);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
        if (instanceMesh.vertexCount <= 0) continue;

        int firstVertex = combined.vertexCount;
        int vertexCount = instanceMesh.vertexCount;
        const FloraStructureInstance *structure = &structures[structureIndex];
        if (!MergeMeshData(&combined, &instanceMesh) ||
            !AppendFloraVisualInstance(
                &instances, &instanceCount, (FloraVisualInstance){
                    .firstVertex = firstVertex,
                    .vertexCount = vertexCount,
                    .anchor = {
                        (float)structure->rootX + 0.5f,
                        (float)(structure->groundY - layerY) + 1.0f,
                        (float)structure->rootZ + 0.5f
                    },
                    .height = (float)(structure->maxY - structure->groundY),
                    .windResponse = structure->windResponse
                })) {
            FreeMeshData(&instanceMesh);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
    }

    if (combined.vertexCount <= 0) {
        free(instances);
        return false;
    }
    *outMesh = combined;
    *outInstances = instances;
    *outInstanceCount = instanceCount;
    return true;
}

bool BuildChunkFloraMeshData(
    const Chunk *chunk, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    if (!chunk || !outMesh || !outInstances || !outInstanceCount) return false;
    Mesh combined = { 0 };
    FloraVisualInstance *instances = NULL;
    int instanceCount = 0;
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        const ChunkSection *section = chunk->sections[sectionIndex];
        int sectionY = section->sectionY;
        Mesh part = { 0 };
        FloraVisualInstance *partInstances = NULL;
        int partCount = 0;
        if (!BuildChunkFloraMeshDataFromSnapshot(
                section->blocks, sectionY * SURFACE_SECTION_HEIGHT,
                chunk->cx, chunk->cz, chunk->floraStructures,
                chunk->floraStructureCount, faces, nearbyTorchIndices,
                nearbyTorchCount, &part, &partInstances, &partCount)) continue;
        float layerY = (float)(sectionY * SURFACE_SECTION_HEIGHT);
        for (int vertex = 0; vertex < part.vertexCount; vertex++) {
            part.vertices[vertex * 3 + 1] += layerY;
        }
        for (int i = 0; i < partCount; i++) {
            partInstances[i].anchor.y += layerY;
        }
        int firstVertex = combined.vertexCount;
        for (int i = 0; i < partCount; i++) {
            partInstances[i].firstVertex += firstVertex;
            if (!AppendFloraVisualInstance(&instances, &instanceCount,
                                           partInstances[i])) {
                free(partInstances);
                FreeMeshData(&part);
                FreeMeshData(&combined);
                free(instances);
                return false;
            }
        }
        free(partInstances);
        if (!MergeMeshData(&combined, &part)) {
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
    }
    if (combined.vertexCount <= 0) {
        free(instances);
        return false;
    }
    *outMesh = combined;
    *outInstances = instances;
    *outInstanceCount = instanceCount;
    return true;
}
