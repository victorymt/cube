#include "ship_locator.h"

#include "raymath.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static ShipLocatorRecord shipLocator = { 0 };

static int ClampCoordinate(int64_t value)
{
    if (value > INT_MAX) return INT_MAX;
    if (value < INT_MIN) return INT_MIN;
    return (int)value;
}

static bool ContextIsValid(ShipLocatorContext context)
{
    if (context.dimension < WORLD_DIMENSION_HOME ||
        context.dimension > WORLD_DIMENSION_NETHER) return false;
    if (context.dimension == WORLD_DIMENSION_PLANET) return context.surfaceId != 0u;
    return context.surfaceId == 0u;
}

static bool CoordinateIsValid(WorldDimension dimension, int y)
{
    switch (dimension) {
    case WORLD_DIMENSION_HOME:
    case WORLD_DIMENSION_PLANET:
        return y >= SURFACE_MIN_Y && y < SURFACE_MAX_Y_EXCLUSIVE;
    case WORLD_DIMENSION_SPACE:
        return y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP;
    case WORLD_DIMENSION_NETHER:
        return y >= NETHER_LAYER_Y && y < NETHER_LAYER_TOP;
    default:
        return false;
    }
}

static bool LocationIsValid(const char *location)
{
    if (!location || location[0] == '\0') return false;
    return memchr(location, '\0', SHIP_LOCATOR_LOCATION_SIZE) != NULL;
}

static bool RecordIsValid(const ShipLocatorRecord *record)
{
    if (!record) return false;
    if (!record->deployed) return true;
    ShipLocatorContext context = {
        .dimension = record->dimension,
        .surfaceId = record->surfaceId
    };
    return ContextIsValid(context) &&
           CoordinateIsValid(record->dimension, record->y) &&
           LocationIsValid(record->location);
}

static bool SameWorld(const ShipLocatorRecord *record,
                      ShipLocatorContext context)
{
    if (record->dimension != context.dimension) return false;
    if (record->dimension == WORLD_DIMENSION_PLANET) {
        return record->surfaceId == context.surfaceId;
    }
    return true;
}

void ShipLocatorReset(void)
{
    shipLocator = (ShipLocatorRecord){ 0 };
}

bool ShipLocatorHasTarget(void)
{
    return shipLocator.deployed;
}

ShipLocatorRecord ShipLocatorGetRecord(void)
{
    return shipLocator;
}

bool ShipLocatorSetRecord(const ShipLocatorRecord *record)
{
    if (!RecordIsValid(record)) return false;
    ShipLocatorRecord next = *record;
    if (!next.deployed) next = (ShipLocatorRecord){ 0 };
    shipLocator = next;
    return true;
}

bool ShipLocatorRecordParked(ShipLocatorContext context, int x, int y, int z,
                             const char *location)
{
    if (!ContextIsValid(context) || !CoordinateIsValid(context.dimension, y) ||
        !location || location[0] == '\0') return false;

    ShipLocatorRecord next = {
        .deployed = true,
        .dimension = context.dimension,
        .surfaceId = context.surfaceId,
        .x = x,
        .y = y,
        .z = z
    };
    if (context.dimension == WORLD_DIMENSION_SPACE) {
        next.x = ClampCoordinate((int64_t)x + context.spaceOriginX);
        next.z = ClampCoordinate((int64_t)z + context.spaceOriginZ);
    }
    snprintf(next.location, sizeof(next.location), "%s", location);
    if (!RecordIsValid(&next)) return false;
    shipLocator = next;
    return true;
}

bool ShipLocatorRemoveIfMatches(ShipLocatorContext context, int x, int y, int z)
{
    if (!shipLocator.deployed || !ContextIsValid(context) ||
        !SameWorld(&shipLocator, context)) return false;
    if (context.dimension == WORLD_DIMENSION_SPACE) {
        x = ClampCoordinate((int64_t)x + context.spaceOriginX);
        z = ClampCoordinate((int64_t)z + context.spaceOriginZ);
    }
    if (shipLocator.x != x || shipLocator.y != y || shipLocator.z != z) return false;
    ShipLocatorReset();
    return true;
}

bool ShipLocatorResolve(ShipLocatorContext context, Vector3 observer,
                        ShipLocatorTarget *out)
{
    if (!out) return false;
    *out = (ShipLocatorTarget){ 0 };
    if (!shipLocator.deployed || !ContextIsValid(context) ||
        !isfinite(observer.x) || !isfinite(observer.y) || !isfinite(observer.z)) {
        return false;
    }

    out->dimension = shipLocator.dimension;
    out->surfaceId = shipLocator.surfaceId;
    snprintf(out->location, sizeof(out->location), "%s", shipLocator.location);
    if (!SameWorld(&shipLocator, context)) {
        out->status = SHIP_LOCATOR_TARGET_REMOTE;
        return true;
    }

    int localX = shipLocator.x;
    int localZ = shipLocator.z;
    if (shipLocator.dimension == WORLD_DIMENSION_SPACE) {
        localX = ClampCoordinate((int64_t)shipLocator.x - context.spaceOriginX);
        localZ = ClampCoordinate((int64_t)shipLocator.z - context.spaceOriginZ);
    }
    out->status = SHIP_LOCATOR_TARGET_LOCAL;
    out->blockX = localX;
    out->blockY = shipLocator.y;
    out->blockZ = localZ;
    out->position = (Vector3){
        (float)localX + 0.5f,
        (float)shipLocator.y + 0.5f,
        (float)localZ + 0.5f
    };
    out->distance = Vector3Distance(observer, out->position);
    if (!isfinite(out->distance)) {
        *out = (ShipLocatorTarget){ 0 };
        return false;
    }
    return true;
}

bool ShipLocatorSaveState(FILE *file)
{
    if (!file || !RecordIsValid(&shipLocator)) return false;
    uint8_t deployed = shipLocator.deployed ? 1u : 0u;
    uint32_t dimension = (uint32_t)shipLocator.dimension;
    int32_t x = shipLocator.x;
    int32_t y = shipLocator.y;
    int32_t z = shipLocator.z;
    char location[SHIP_LOCATOR_LOCATION_SIZE] = { 0 };
    if (shipLocator.deployed) {
        snprintf(location, sizeof(location), "%s", shipLocator.location);
    }
    return fwrite(&deployed, sizeof(deployed), 1, file) == 1 &&
           fwrite(&dimension, sizeof(dimension), 1, file) == 1 &&
           fwrite(&shipLocator.surfaceId, sizeof(shipLocator.surfaceId), 1, file) == 1 &&
           fwrite(&x, sizeof(x), 1, file) == 1 &&
           fwrite(&y, sizeof(y), 1, file) == 1 &&
           fwrite(&z, sizeof(z), 1, file) == 1 &&
           fwrite(location, sizeof(location), 1, file) == 1;
}

bool ShipLocatorReadStateForSpaceLayer(FILE *file, ShipLocatorRecord *out,
                                       int storedSpaceLayerY)
{
    if (!file || !out) return false;
    uint8_t deployed = 0;
    uint32_t dimension = 0;
    uint32_t surfaceId = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    char location[SHIP_LOCATOR_LOCATION_SIZE] = { 0 };
    if (fread(&deployed, sizeof(deployed), 1, file) != 1 || deployed > 1u ||
        fread(&dimension, sizeof(dimension), 1, file) != 1 ||
        fread(&surfaceId, sizeof(surfaceId), 1, file) != 1 ||
        fread(&x, sizeof(x), 1, file) != 1 ||
        fread(&y, sizeof(y), 1, file) != 1 ||
        fread(&z, sizeof(z), 1, file) != 1 ||
        fread(location, sizeof(location), 1, file) != 1) {
        return false;
    }
    if (dimension > (uint32_t)WORLD_DIMENSION_NETHER ||
        memchr(location, '\0', sizeof(location)) == NULL) return false;

    ShipLocatorRecord next = {
        .deployed = deployed != 0,
        .dimension = (WorldDimension)dimension,
        .surfaceId = surfaceId,
        .x = x,
        .y = y,
        .z = z
    };
    memcpy(next.location, location, sizeof(next.location));
    if (next.deployed && next.dimension == WORLD_DIMENSION_SPACE) {
        int64_t storedTop = (int64_t)storedSpaceLayerY + SPACE_LAYER_HEIGHT;
        if ((int64_t)next.y < storedSpaceLayerY ||
            (int64_t)next.y >= storedTop) return false;
        next.y = ClampCoordinate((int64_t)next.y - storedSpaceLayerY +
                                 SPACE_LAYER_Y);
    }
    if (!RecordIsValid(&next)) return false;
    if (!next.deployed) next = (ShipLocatorRecord){ 0 };
    *out = next;
    return true;
}

bool ShipLocatorReadState(FILE *file, ShipLocatorRecord *out)
{
    return ShipLocatorReadStateForSpaceLayer(file, out, SPACE_LAYER_Y);
}

bool ShipLocatorLoadState(FILE *file)
{
    ShipLocatorRecord next = { 0 };
    if (!ShipLocatorReadState(file, &next)) return false;
    shipLocator = next;
    return true;
}

ShipLocatorMarkerLayout ShipLocatorMarkerLayoutEvaluate(
    Vector2 projected, bool behind, int screenWidth, int screenHeight,
    float margin)
{
    ShipLocatorMarkerLayout layout = { 0 };
    if (screenWidth <= 0 || screenHeight <= 0 || !isfinite(projected.x) ||
        !isfinite(projected.y) || !isfinite(margin)) return layout;

    float maxMargin = fmaxf(0.0f, fminf((float)screenWidth, (float)screenHeight) * 0.5f - 1.0f);
    margin = fminf(fmaxf(margin, 0.0f), maxMargin);
    float cx = (float)screenWidth * 0.5f;
    float cy = (float)screenHeight * 0.5f;
    float dx = projected.x - cx;
    float dy = projected.y - cy;
    if (behind) {
        dx = -dx;
        dy = -dy;
    }
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) {
        dx = 0.0f;
        dy = -1.0f;
        length = 1.0f;
    }
    layout.direction = (Vector2){ dx / length, dy / length };
    layout.visible = true;
    layout.onScreen = !behind && projected.x >= margin &&
                      projected.x <= (float)screenWidth - margin &&
                      projected.y >= margin &&
                      projected.y <= (float)screenHeight - margin;
    if (layout.onScreen) {
        layout.position = projected;
        return layout;
    }

    float tx = (cx - margin) / fmaxf(fabsf(layout.direction.x), 0.00001f);
    float ty = (cy - margin) / fmaxf(fabsf(layout.direction.y), 0.00001f);
    float scale = fminf(tx, ty);
    layout.position = (Vector2){
        cx + layout.direction.x * scale,
        cy + layout.direction.y * scale
    };
    return layout;
}
