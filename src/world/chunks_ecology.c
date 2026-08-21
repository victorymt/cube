#include "world/chunks_internal.h"

bool DeformFloraMeshInstance(
    float *vertices, const float *baseVertices, int vertexCount,
    const FloraVisualInstance *instance, float targetScale, float blend,
    float sway, float windAngle, float *outScale, bool *outChanged)
{
    if (!outScale || !outChanged) return false;
    *outScale = 1.0f;
    *outChanged = false;
    if (!vertices || !baseVertices || !instance || vertexCount <= 0 ||
        !isfinite(targetScale) || !isfinite(blend) || !isfinite(sway) ||
        !isfinite(windAngle)) {
        return false;
    }

    int firstVertex = instance->firstVertex;
    if (firstVertex < 0 || firstVertex >= vertexCount ||
        instance->vertexCount <= 0) {
        return false;
    }
    int count = instance->vertexCount;
    int available = vertexCount - firstVertex;
    if (count > available) count = available;
    int lastVertex = firstVertex + count;

    float baseTopY = -INFINITY;
    float currentTopY = -INFINITY;
    for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
        const float *base = &baseVertices[vertex * 3];
        const float *current = &vertices[vertex * 3];
        if (!isfinite(base[0]) || !isfinite(base[1]) ||
            !isfinite(base[2]) || !isfinite(current[1])) {
            return false;
        }
        baseTopY = fmaxf(baseTopY, base[1]);
        currentTopY = fmaxf(currentTopY, current[1]);
    }

    float localBaseHeight = baseTopY - instance->anchor.y;
    float instanceHeight = instance->height > 0.001f &&
                           isfinite(instance->height) ?
                           instance->height : localBaseHeight;
    if (!(localBaseHeight > 0.001f) || !isfinite(localBaseHeight) ||
        !(instanceHeight > 0.001f) || !isfinite(instanceHeight)) {
        return false;
    }
    float oldScale = (currentTopY - instance->anchor.y) / localBaseHeight;
    if (!(oldScale > 0.01f) || !isfinite(oldScale)) return false;

    float amount = fminf(fmaxf(blend, 0.0f), 1.0f);
    float boundedTargetScale = fmaxf(targetScale, 0.01f);
    float newScale = amount >= 1.0f ? boundedTargetScale :
                     oldScale + (boundedTargetScale - oldScale) * amount;
    float swayX = cosf(windAngle) * sway;
    float swayZ = sinf(windAngle) * sway;
    bool changed = false;
    for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
        float *current = &vertices[vertex * 3];
        const float *base = &baseVertices[vertex * 3];
        float heightFraction = fminf(fmaxf(
            (base[1] - instance->anchor.y) / instanceHeight, 0.0f), 1.0f);
        float targetX = base[0] + swayX * heightFraction;
        float targetY = instance->anchor.y +
                        (base[1] - instance->anchor.y) * newScale;
        float targetZ = base[2] + swayZ * heightFraction;
        if (fabsf(current[0] - targetX) >= 0.0001f ||
            fabsf(current[1] - targetY) >= 0.0001f ||
            fabsf(current[2] - targetZ) >= 0.0001f) {
            changed = true;
        }
        current[0] = targetX;
        current[1] = targetY;
        current[2] = targetZ;
    }
    *outScale = newScale;
    *outChanged = changed;
    return true;
}

static void UpdateChunkSectionFloraScale(ChunkSection *section,
                                  float elapsed,
                                  float daylight, bool refreshTargets)
{
    if (!section || !section->hasFloraModel ||
        section->floraModel.meshCount <= 0) return;

    Mesh *mesh = &section->floraModel.meshes[0];
    if (!mesh->vertices || mesh->vertexCount <= 0 ||
        !section->floraTargetScales || !section->floraTargetWind ||
        !section->floraTargetWindAngle || !section->floraTargetPresence ||
        !section->floraBaseVertices ||
        !section->floraBaseColors || !section->floraVisualInstances ||
        !mesh->colors ||
        section->floraTargetScaleCount <= 0) return;

    float blend = fminf(elapsed * 1.8f, 1.0f);
    float colorBlend = fminf(elapsed * 2.2f, 1.0f);
    float scaleSum = 0.0f;
    int scaleCount = 0;
    bool changed = false;
    for (int group = 0; group < section->floraTargetScaleCount; group++) {
        const FloraVisualInstance *instance =
            &section->floraVisualInstances[group];
        if (!isfinite(instance->anchor.x) ||
            !isfinite(instance->anchor.z)) continue;
        int cellX = (int)floorf(instance->anchor.x);
        int cellZ = (int)floorf(instance->anchor.z);

        if (refreshTargets && PlanetWorldIsActive()) {
            PlanetLocalEcology local = PlanetEcologyLocalAt(cellX, cellZ, daylight);
            PlanetFloraRuntimeState runtime = PlanetEcologyFloraRuntime(
                local.suitability.floraActivity,
                local.suitability.floraCapacity);
            section->floraTargetScales[group] = runtime.growthScale;
            section->floraTargetPresence[group] = runtime.visualPresence;
            section->floraTargetWind[group] = WeatherFieldSampleAtWorld(
                cellX, cellZ).wind;
            section->floraTargetWindAngle[group] = WeatherWindAngleAtWorld(
                cellX, cellZ);
        } else if (refreshTargets) {
            section->floraTargetScales[group] = 1.0f;
            section->floraTargetPresence[group] = 1.0f;
            section->floraTargetWind[group] = WeatherFieldSampleAtWorld(
                cellX, cellZ).wind;
            section->floraTargetWindAngle[group] = WeatherWindAngleAtWorld(
                cellX, cellZ);
        }

        float phase = (float)(Hash3D(cellX, 0, cellZ) & 4095u) * 0.0015339808f;
        float sway = sinf((float)SpacePeriodicSimulationTime(
                              SpaceElapsedSimulationTime()) * 1.7f + phase) *
                         fmaxf(section->floraTargetWind[group], 0.0f) * 0.07f *
                         fmaxf(instance->windResponse, 0.0f);
        float newScale = 1.0f;
        bool instanceChanged = false;
        if (!DeformFloraMeshInstance(
                mesh->vertices, section->floraBaseVertices, mesh->vertexCount,
                instance, section->floraTargetScales[group], blend, sway,
                section->floraTargetWindAngle[group], &newScale,
                &instanceChanged)) {
            continue;
        }
        scaleSum += newScale;
        scaleCount++;
        if (instanceChanged) changed = true;
    }
    if (changed) {
        UpdateMeshBuffer(*mesh, 0, mesh->vertices,
                         mesh->vertexCount * 3 * (int)sizeof(float), 0);
    }
    if (ApplyFloraMeshInstancePresenceColors(
            mesh->colors, section->floraBaseColors, mesh->vertexCount,
            section->floraTargetPresence, section->floraVisualInstances,
            section->floraTargetScaleCount, colorBlend)) {
        UpdateMeshBuffer(*mesh, 3, mesh->colors,
                         mesh->vertexCount * 4 * (int)sizeof(unsigned char), 0);
    }
    if (scaleCount > 0) section->floraVisualScale = scaleSum / (float)scaleCount;
}

static bool ApplyFloraMeshColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, const FloraVisualInstance *instances,
    int targetCount, float blend)
{
    static const float dormantFactors[3] = { 0.55f, 0.42f, 0.32f };
    if (!colors || !baseColors || !targetPresence || vertexCount <= 0 ||
        targetCount <= 0) {
        return false;
    }

    float amount = fminf(fmaxf(blend, 0.0f), 1.0f);
    bool changed = false;
    for (int group = 0; group < targetCount; group++) {
        int firstVertex = instances ? instances[group].firstVertex : group * 12;
        int count = instances ? instances[group].vertexCount : 12;
        if (firstVertex < 0 || firstVertex >= vertexCount || count <= 0) {
            continue;
        }
        int lastVertex = firstVertex + count;
        if (lastVertex > vertexCount) lastVertex = vertexCount;
        float presence = fminf(fmaxf(targetPresence[group], 0.0f), 1.0f);
        for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
            int colorIndex = vertex * 4;
            for (int channel = 0; channel < 3; channel++) {
                float base = (float)baseColors[colorIndex + channel];
                float dormant = base * dormantFactors[channel];
                float target = dormant + (base - dormant) * presence;
                float current = (float)colors[colorIndex + channel];
                unsigned char next = (unsigned char)lroundf(
                    current + (target - current) * amount);
                if (next != colors[colorIndex + channel]) {
                    colors[colorIndex + channel] = next;
                    changed = true;
                }
            }
            if (colors[colorIndex + 3] != baseColors[colorIndex + 3]) {
                colors[colorIndex + 3] = baseColors[colorIndex + 3];
                changed = true;
            }
        }
    }
    return changed;
}

bool ApplyFloraMeshPresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, int targetCount, float blend)
{
    return ApplyFloraMeshColors(colors, baseColors, vertexCount,
                                targetPresence, NULL, targetCount, blend);
}

bool ApplyFloraMeshInstancePresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, const FloraVisualInstance *instances,
    int instanceCount, float blend)
{
    if (!instances) return false;
    return ApplyFloraMeshColors(colors, baseColors, vertexCount,
                                targetPresence, instances, instanceCount,
                                blend);
}

void ChunksUpdateEcologyVisuals(float dt, float daylight)
{
    bool planetWorld = PlanetWorldIsActive();
    float elapsed = fmaxf(dt, 0.0f);
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;
        if (chunk->activeLod != CHUNK_LOD_EXACT) continue;

        chunk->floraSampleTimer -= elapsed;
        bool refreshTargets = chunk->floraSampleTimer <= 0.0f;
        if (refreshTargets) {
            int centerX = chunk->cx * CHUNK_SIZE + CHUNK_SIZE / 2;
            int centerZ = chunk->cz * CHUNK_SIZE + CHUNK_SIZE / 2;
            chunk->floraWindAngle = WeatherWindAngleAtWorld(centerX, centerZ);
            if (planetWorld) {
                PlanetLocalEcology local = PlanetEcologyLocalAt(
                    centerX, centerZ, daylight);
                chunk->floraActivity = local.suitability.floraActivity;
                chunk->floraCapacity = local.suitability.floraCapacity;
            } else {
                chunk->floraActivity = 1.0f;
                chunk->floraCapacity = 1.0f;
            }

            unsigned int stagger = Hash3D(chunk->cx, 0, chunk->cz) & 255u;
            chunk->floraSampleTimer = 0.75f + (float)stagger / 510.0f;
        }

        for (int index = 0; index < chunk->sectionCount; index++) {
            UpdateChunkSectionFloraScale(chunk->sections[index], elapsed,
                                         daylight, refreshTargets);
        }
    }
}
