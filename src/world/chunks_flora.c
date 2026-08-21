#include "world/chunks_internal.h"

#include "ecology/flora_taxa.h"
#include "world/home_tree_shape.h"

#include <limits.h>

static bool BuildFloraMeshDataWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh)
{
    return BuildMeshDataFilteredWithSnapshot(
        blocks, height, layerY, chunkX, chunkZ, false, false, true, false,
        faces, nearbyTorchIndices, nearbyTorchCount, boundary, outMesh);
}

bool BuildFloraMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildFloraMeshDataWithSnapshot(
        blocks, height, layerY, chunkX, chunkZ, faces,
        nearbyTorchIndices, nearbyTorchCount, NULL, outMesh);
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
    case FLORA_STRUCTURE_HOME_TREE:
        return HomeTreeShapeBlockAt(
            &(HomeTreeShapeSpec){
                .rootX = structure->rootX,
                .baseY = base,
                .rootZ = structure->rootZ,
                .taxonId = structure->taxonId,
                .shapeHash = structure->shapeHash,
                .primaryBlock = structure->primaryBlock,
                .accentBlock = structure->accentBlock
            },
            worldX, y, worldZ, outBlock);
    case FLORA_STRUCTURE_ALIEN_CANOPY: {
        int trunkHeight = structure->maxY - base - 1;
        int radius = structure->maxX - structure->rootX;
        int distance = abs(offsetX) + abs(offsetZ);
        if (offsetX == 0 && offsetZ == 0 &&
            y == base + trunkHeight + 1) {
            *outBlock = BLOCK_LUMINOUS_POD;
            return true;
        }
        if ((y == base + trunkHeight - 1 && distance <= radius + 1) ||
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
        int height = structure->maxY - base + 1;
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
        int stemHeight = structure->maxY - base - 1;
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
        int height = structure->maxY - base;
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

bool ChunkFloraStructureOwnsBlock(
    const Chunk *chunk, int worldX, int y, int worldZ, BlockType block)
{
    return chunk && FloraStructureOwnerAt(
        chunk->floraStructures, chunk->floraStructureCount,
        worldX, y, worldZ, block) >= 0;
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

typedef struct FloraMeshBuilder {
    Mesh mesh;
    int vertexCapacity;
} FloraMeshBuilder;

typedef struct FloraInstanceBuilder {
    FloraVisualInstance *data;
    int count;
    int capacity;
} FloraInstanceBuilder;

static void FloraMeshBuilderRelease(FloraMeshBuilder *builder)
{
    if (!builder) return;
    FreeMeshData(&builder->mesh);
    builder->vertexCapacity = 0;
}

static bool FloraMeshBuilderReserve(
    FloraMeshBuilder *builder, int minimumVertexCount)
{
    if (minimumVertexCount <= builder->vertexCapacity) return true;

    int capacity = builder->vertexCapacity > 0 ? builder->vertexCapacity : 12;
    while (capacity < minimumVertexCount) {
        if (capacity > INT_MAX - capacity) {
            capacity = minimumVertexCount;
            break;
        }
        capacity *= 2;
    }

    float *vertices = malloc((size_t)capacity * 3u * sizeof(float));
    float *texcoords = malloc((size_t)capacity * 2u * sizeof(float));
    float *texcoords2 = malloc((size_t)capacity * 2u * sizeof(float));
    float *normals = malloc((size_t)capacity * 3u * sizeof(float));
    unsigned char *colors = malloc((size_t)capacity * 4u);
    if (!vertices || !texcoords || !texcoords2 || !normals || !colors) {
        free(vertices);
        free(texcoords);
        free(texcoords2);
        free(normals);
        free(colors);
        return false;
    }

    int vertexCount = builder->mesh.vertexCount;
    if (vertexCount > 0) {
        memcpy(vertices, builder->mesh.vertices,
               (size_t)vertexCount * 3u * sizeof(float));
        memcpy(texcoords, builder->mesh.texcoords,
               (size_t)vertexCount * 2u * sizeof(float));
        memcpy(texcoords2, builder->mesh.texcoords2,
               (size_t)vertexCount * 2u * sizeof(float));
        memcpy(normals, builder->mesh.normals,
               (size_t)vertexCount * 3u * sizeof(float));
        memcpy(colors, builder->mesh.colors, (size_t)vertexCount * 4u);
    }
    free(builder->mesh.vertices);
    free(builder->mesh.texcoords);
    free(builder->mesh.texcoords2);
    free(builder->mesh.normals);
    free(builder->mesh.colors);
    builder->mesh.vertices = vertices;
    builder->mesh.texcoords = texcoords;
    builder->mesh.texcoords2 = texcoords2;
    builder->mesh.normals = normals;
    builder->mesh.colors = colors;
    builder->vertexCapacity = capacity;
    return true;
}

static bool FloraMeshBuilderAppend(
    FloraMeshBuilder *builder, Mesh *source)
{
    if (!builder || !source) return false;
    if (source->vertexCount <= 0) {
        FreeMeshData(source);
        return true;
    }
    if (builder->mesh.vertexCount <= 0) {
        builder->mesh = *source;
        builder->vertexCapacity = source->vertexCount;
        *source = (Mesh){ 0 };
        return true;
    }

    if (source->vertexCount > INT_MAX - builder->mesh.vertexCount) {
        return false;
    }
    int oldVertexCount = builder->mesh.vertexCount;
    int newVertexCount = oldVertexCount + source->vertexCount;
    if (!FloraMeshBuilderReserve(builder, newVertexCount)) return false;

    memcpy(builder->mesh.vertices + oldVertexCount * 3,
           source->vertices,
           (size_t)source->vertexCount * 3u * sizeof(float));
    memcpy(builder->mesh.texcoords + oldVertexCount * 2,
           source->texcoords,
           (size_t)source->vertexCount * 2u * sizeof(float));
    memcpy(builder->mesh.texcoords2 + oldVertexCount * 2,
           source->texcoords2,
           (size_t)source->vertexCount * 2u * sizeof(float));
    memcpy(builder->mesh.normals + oldVertexCount * 3,
           source->normals,
           (size_t)source->vertexCount * 3u * sizeof(float));
    memcpy(builder->mesh.colors + oldVertexCount * 4,
           source->colors,
           (size_t)source->vertexCount * 4u);
    builder->mesh.vertexCount = newVertexCount;
    builder->mesh.triangleCount += source->triangleCount;
    FreeMeshData(source);
    return true;
}

static void FloraMeshBuilderTake(FloraMeshBuilder *builder, Mesh *outMesh)
{
    *outMesh = builder->mesh;
    *builder = (FloraMeshBuilder){ 0 };
}

static void FloraInstanceBuilderRelease(FloraInstanceBuilder *builder)
{
    if (!builder) return;
    free(builder->data);
    *builder = (FloraInstanceBuilder){ 0 };
}

static bool FloraInstanceBuilderReserve(
    FloraInstanceBuilder *builder, int minimumCount)
{
    if (minimumCount <= builder->capacity) return true;
    int capacity = builder->capacity > 0 ? builder->capacity : 8;
    while (capacity < minimumCount) {
        if (capacity > INT_MAX - capacity) {
            capacity = minimumCount;
            break;
        }
        capacity *= 2;
    }
    FloraVisualInstance *resized = realloc(
        builder->data, (size_t)capacity * sizeof(*resized));
    if (!resized) return false;
    builder->data = resized;
    builder->capacity = capacity;
    return true;
}

static bool AppendFloraVisualInstance(
    FloraInstanceBuilder *builder,
    FloraVisualInstance instance)
{
    if (!builder || builder->count == INT_MAX) return false;
    if (!FloraInstanceBuilderReserve(builder, builder->count + 1)) {
        return false;
    }
    builder->data[builder->count++] = instance;
    return true;
}

static bool FloraInstanceBuilderAppendArray(
    FloraInstanceBuilder *builder, const FloraVisualInstance *instances,
    int count)
{
    if (!builder || count < 0 || (count > 0 && !instances) ||
        count > INT_MAX - builder->count) {
        return false;
    }
    if (count == 0) return true;
    if (!FloraInstanceBuilderReserve(builder, builder->count + count)) {
        return false;
    }
    memcpy(builder->data + builder->count, instances,
           (size_t)count * sizeof(*instances));
    builder->count += count;
    return true;
}

static bool AppendPlantMeshInstances(
    FloraInstanceBuilder *builder, const Mesh *mesh, int firstVertexOffset,
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int startX, int startZ)
{
    for (int firstVertex = 0; firstVertex < mesh->vertexCount;
         ) {
        float firstX = mesh->vertices[firstVertex * 3];
        float firstY = mesh->vertices[firstVertex * 3 + 1];
        float firstZ = mesh->vertices[firstVertex * 3 + 2];
        int lx = (int)floorf(firstX) - startX;
        int localY = (int)floorf(firstY);
        int lz = (int)floorf(firstZ) - startZ;
        FloraTaxonId taxonId = FLORA_TAXON_COUNT;
        const FloraTaxon *taxon = NULL;
        BlockType block = BLOCK_AIR;
        if (lx >= 0 && lx < CHUNK_SIZE &&
            localY >= 0 && localY < SURFACE_SECTION_HEIGHT &&
            lz >= 0 && lz < CHUNK_SIZE) {
            block = (BlockType)blocks[lx][localY][lz];
            taxonId = FloraTaxonIdForBlock(block);
            taxon = FloraTaxonAt(taxonId);
        }
        int vertexCount = BlockRenderShapeFor(block) == BLOCK_RENDER_CROSS
            ? 18 : 12;
        if (vertexCount > mesh->vertexCount - firstVertex) {
            vertexCount = mesh->vertexCount - firstVertex;
        }
        float groundY = INFINITY;
        float topY = -INFINITY;
        for (int vertex = firstVertex;
             vertex < firstVertex + vertexCount; vertex++) {
            groundY = fminf(groundY, mesh->vertices[vertex * 3 + 1]);
            topY = fmaxf(topY, mesh->vertices[vertex * 3 + 1]);
        }
        if (!AppendFloraVisualInstance(
                builder, (FloraVisualInstance){
                    .firstVertex = firstVertexOffset + firstVertex,
                    .vertexCount = vertexCount,
                    .taxonId = taxonId,
                    .anchor = {
                        floorf(firstX) + 0.5f,
                        groundY,
                        floorf(firstZ) + 0.5f
                    },
                    .height = topY - groundY,
                    .windResponse = taxon ? taxon->windResponse :
                        (topY - groundY < 0.10f ? 0.05f : 1.0f)
                })) {
            return false;
        }
        firstVertex += vertexCount;
    }
    return true;
}

bool BuildChunkSurfaceSolidMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, int greedyMaxSpan,
    const SurfaceBoundarySnapshot *boundary,
    Mesh *outMesh)
{
    unsigned short solidBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    CopyBlocksWithoutFloraStructures(
        solidBlocks, blocks, layerY, chunkX, chunkZ, structures, structureCount);
    return BuildMeshDataFilteredWithSnapshotSpan(
        (const unsigned short (*)[CHUNK_SIZE])solidBlocks,
        SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ,
        false, false, false, false, faces,
        nearbyTorchIndices, nearbyTorchCount, greedyMaxSpan, boundary,
        outMesh);
}

bool BuildChunkSurfaceWaterMeshDataWithSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char *waterVolumes, int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, const SurfaceBoundarySnapshot *boundary,
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
    int nearbyTorchCount, const SurfaceBoundarySnapshot *boundary,
    Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    *outMesh = (Mesh){ 0 };
    *outInstances = NULL;
    *outInstanceCount = 0;

    unsigned short floraBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    CopyBlocksWithoutFloraStructures(
        floraBlocks, blocks, layerY, chunkX, chunkZ, structures, structureCount);

    FloraMeshBuilder combined = { 0 };
    FloraInstanceBuilder instances = { 0 };
    Mesh plants = { 0 };
    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;
    if (BuildFloraMeshDataWithSnapshot(
            (const unsigned short (*)[CHUNK_SIZE])floraBlocks,
            SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
            nearbyTorchIndices, nearbyTorchCount, boundary, &plants)) {
        if (!AppendPlantMeshInstances(
                &instances, &plants, 0, floraBlocks, startX, startZ) ||
            !FloraMeshBuilderAppend(&combined, &plants)) {
            FreeMeshData(&plants);
            FloraMeshBuilderRelease(&combined);
            FloraInstanceBuilderRelease(&instances);
            return false;
        }
    }

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

        Mesh solid = { 0 };
        Mesh transparent = { 0 };
        Mesh crossed = { 0 };
        FloraMeshBuilder instanceMesh = { 0 };
        if (BuildMeshDataFilteredWithSnapshot(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ,
                false, false, false, false, faces,
                nearbyTorchIndices, nearbyTorchCount, boundary, &solid) &&
            !FloraMeshBuilderAppend(&instanceMesh, &solid)) {
            FreeMeshData(&solid);
            FloraMeshBuilderRelease(&instanceMesh);
            FloraMeshBuilderRelease(&combined);
            FloraInstanceBuilderRelease(&instances);
            return false;
        }
        if (BuildMeshDataFilteredWithSnapshot(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ,
                true, false, false, true, faces,
                nearbyTorchIndices, nearbyTorchCount, boundary,
                &transparent) &&
            !FloraMeshBuilderAppend(&instanceMesh, &transparent)) {
            FreeMeshData(&transparent);
            FloraMeshBuilderRelease(&instanceMesh);
            FloraMeshBuilderRelease(&combined);
            FloraInstanceBuilderRelease(&instances);
            return false;
        }
        if (BuildFloraMeshDataWithSnapshot(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
                nearbyTorchIndices, nearbyTorchCount, boundary, &crossed) &&
            !FloraMeshBuilderAppend(&instanceMesh, &crossed)) {
            FreeMeshData(&crossed);
            FloraMeshBuilderRelease(&instanceMesh);
            FloraMeshBuilderRelease(&combined);
            FloraInstanceBuilderRelease(&instances);
            return false;
        }
        if (instanceMesh.mesh.vertexCount <= 0) {
            FloraMeshBuilderRelease(&instanceMesh);
            continue;
        }

        int firstVertex = combined.mesh.vertexCount;
        int vertexCount = instanceMesh.mesh.vertexCount;
        const FloraStructureInstance *structure = &structures[structureIndex];
        Mesh completedInstanceMesh = { 0 };
        FloraMeshBuilderTake(&instanceMesh, &completedInstanceMesh);
        if (!FloraMeshBuilderAppend(&combined, &completedInstanceMesh) ||
            !AppendFloraVisualInstance(
                &instances, (FloraVisualInstance){
                    .firstVertex = firstVertex,
                    .vertexCount = vertexCount,
                    .taxonId = structure->taxonId,
                    .anchor = {
                        (float)structure->rootX + 0.5f,
                        (float)(structure->groundY - layerY) + 1.0f,
                        (float)structure->rootZ + 0.5f
                    },
                    .height = (float)(structure->maxY - structure->groundY),
                    .windResponse = structure->windResponse
                })) {
            FreeMeshData(&completedInstanceMesh);
            FloraMeshBuilderRelease(&instanceMesh);
            FloraMeshBuilderRelease(&combined);
            FloraInstanceBuilderRelease(&instances);
            return false;
        }
    }

    if (combined.mesh.vertexCount <= 0) {
        FloraMeshBuilderRelease(&combined);
        FloraInstanceBuilderRelease(&instances);
        return false;
    }
    FloraMeshBuilderTake(&combined, outMesh);
    *outInstances = instances.data;
    *outInstanceCount = instances.count;
    instances = (FloraInstanceBuilder){ 0 };
    return true;
}

bool BuildChunkFloraMeshData(
    const Chunk *chunk, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    if (!chunk || !outMesh || !outInstances || !outInstanceCount) return false;
    *outMesh = (Mesh){ 0 };
    *outInstances = NULL;
    *outInstanceCount = 0;

    FloraMeshBuilder combined = { 0 };
    FloraInstanceBuilder instances = { 0 };
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
                nearbyTorchCount, NULL, &part, &partInstances,
                &partCount)) continue;
        float layerY = (float)(sectionY * SURFACE_SECTION_HEIGHT);
        for (int vertex = 0; vertex < part.vertexCount; vertex++) {
            part.vertices[vertex * 3 + 1] += layerY;
        }
        for (int i = 0; i < partCount; i++) {
            partInstances[i].anchor.y += layerY;
        }
        int firstVertex = combined.mesh.vertexCount;
        for (int i = 0; i < partCount; i++) {
            partInstances[i].firstVertex += firstVertex;
        }
        if (!FloraInstanceBuilderAppendArray(
                &instances, partInstances, partCount)) {
            free(partInstances);
            FreeMeshData(&part);
            FloraMeshBuilderRelease(&combined);
            FloraInstanceBuilderRelease(&instances);
            return false;
        }
        free(partInstances);
        if (!FloraMeshBuilderAppend(&combined, &part)) {
            FreeMeshData(&part);
            FloraMeshBuilderRelease(&combined);
            FloraInstanceBuilderRelease(&instances);
            return false;
        }
    }
    if (combined.mesh.vertexCount <= 0) {
        FloraMeshBuilderRelease(&combined);
        FloraInstanceBuilderRelease(&instances);
        return false;
    }
    FloraMeshBuilderTake(&combined, outMesh);
    *outInstances = instances.data;
    *outInstanceCount = instances.count;
    return true;
}
