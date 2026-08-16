#include "gameplay/map_markers.h"

#include "world/surface_topology.h"

#include <math.h>
#include <string.h>

#define MAP_MARKER_STATE_VERSION 1u

static MapMarkerState mapMarkers = { .nextId = 1u };

static size_t BoundedLength(const char *text, size_t capacity)
{
    if (!text) return 0u;
    size_t length = 0u;
    while (length < capacity && text[length] != '\0') length++;
    return length;
}

static bool SameSurface(MapMarkerSurface a, MapMarkerSurface b)
{
    return a.dimension == b.dimension && a.surfaceId == b.surfaceId;
}

static bool SurfaceIsValid(MapMarkerSurface surface)
{
    if (surface.dimension == WORLD_DIMENSION_HOME) return surface.surfaceId == 0u;
    if (surface.dimension == WORLD_DIMENSION_PLANET) return surface.surfaceId != 0u;
    return false;
}

static bool DecodeUtf8(const unsigned char *text, size_t remaining,
                       int *outCodepoint, size_t *outLength)
{
    if (!text || remaining == 0u || !outCodepoint || !outLength) return false;
    unsigned char first = text[0];
    int codepoint = 0;
    size_t length = 0u;
    if (first <= 0x7fu) {
        codepoint = first;
        length = 1u;
    } else if (first >= 0xc2u && first <= 0xdfu) {
        codepoint = first & 0x1f;
        length = 2u;
    } else if (first >= 0xe0u && first <= 0xefu) {
        codepoint = first & 0x0f;
        length = 3u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        codepoint = first & 0x07;
        length = 4u;
    } else {
        return false;
    }
    if (remaining < length) return false;
    for (size_t i = 1u; i < length; i++) {
        if ((text[i] & 0xc0u) != 0x80u) return false;
        codepoint = (codepoint << 6) | (text[i] & 0x3f);
    }
    if ((length == 2u && codepoint < 0x80) ||
        (length == 3u && codepoint < 0x800) ||
        (length == 4u && codepoint < 0x10000) ||
        codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        return false;
    }
    *outCodepoint = codepoint;
    *outLength = length;
    return true;
}

static bool CodepointIsNameCharacter(int codepoint)
{
    return codepoint >= 32 && codepoint != 127 &&
           codepoint <= 0x10ffff &&
           !(codepoint >= 0xd800 && codepoint <= 0xdfff);
}

static size_t EncodeUtf8(int codepoint, char out[4])
{
    if (codepoint <= 0x7f) {
        out[0] = (char)codepoint;
        return 1u;
    }
    if (codepoint <= 0x7ff) {
        out[0] = (char)(0xc0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2u;
    }
    if (codepoint <= 0xffff) {
        out[0] = (char)(0xe0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        out[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3u;
    }
    out[0] = (char)(0xf0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    out[3] = (char)(0x80 | (codepoint & 0x3f));
    return 4u;
}

bool MapMarkerNameIsValid(const char *name)
{
    if (!name) return false;
    size_t length = BoundedLength(name, MAP_MARKER_NAME_SIZE);
    if (length == 0u || length >= MAP_MARKER_NAME_SIZE) return false;
    bool visible = false;
    for (size_t offset = 0u; offset < length;) {
        int codepoint = 0;
        size_t bytes = 0u;
        if (!DecodeUtf8((const unsigned char *)name + offset,
                        length - offset, &codepoint, &bytes) ||
            !CodepointIsNameCharacter(codepoint)) {
            return false;
        }
        if (codepoint != ' ') visible = true;
        offset += bytes;
    }
    return visible;
}

bool MapMarkerNameAppendCodepoint(char *name, size_t capacity, int codepoint)
{
    if (!name || capacity == 0u || !CodepointIsNameCharacter(codepoint)) {
        return false;
    }
    size_t current = BoundedLength(name, capacity);
    if (current >= capacity) return false;
    char encoded[4];
    size_t bytes = EncodeUtf8(codepoint, encoded);
    if (current + bytes >= capacity) return false;
    memcpy(name + current, encoded, bytes);
    name[current + bytes] = '\0';
    return true;
}

bool MapMarkerNameAppendUtf8(char *name, size_t capacity, const char *text)
{
    if (!name || !text || capacity == 0u) return false;
    char next[MAP_MARKER_NAME_SIZE] = { 0 };
    if (capacity > sizeof(next)) return false;
    size_t current = BoundedLength(name, capacity);
    if (current >= capacity) return false;
    memcpy(next, name, current + 1u);
    size_t length = strlen(text);
    for (size_t offset = 0u; offset < length;) {
        int codepoint = 0;
        size_t bytes = 0u;
        if (!DecodeUtf8((const unsigned char *)text + offset,
                        length - offset, &codepoint, &bytes) ||
            !MapMarkerNameAppendCodepoint(next, capacity, codepoint)) {
            return false;
        }
        offset += bytes;
    }
    memcpy(name, next, capacity);
    return true;
}

void MapMarkerNameBackspace(char *name)
{
    if (!name) return;
    size_t length = strlen(name);
    if (length == 0u) return;
    size_t offset = length - 1u;
    while (offset > 0u && (((unsigned char)name[offset] & 0xc0u) == 0x80u)) {
        offset--;
    }
    name[offset] = '\0';
}

Color MapMarkerColorValue(MapMarkerColor color)
{
    static const Color colors[MAP_MARKER_COLOR_COUNT] = {
        { 238, 84, 76, 255 }, { 244, 178, 62, 255 },
        { 91, 205, 118, 255 }, { 68, 198, 214, 255 },
        { 87, 139, 235, 255 }, { 218, 91, 190, 255 }
    };
    if (color < 0 || color >= MAP_MARKER_COLOR_COUNT) return colors[0];
    return colors[color];
}

void MapMarkersEmptyState(MapMarkerState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->nextId = 1u;
}

void MapMarkersReset(void)
{
    MapMarkersEmptyState(&mapMarkers);
}

static int FindIndexInState(const MapMarkerState *state, uint32_t id)
{
    if (!state || id == 0u) return -1;
    for (uint32_t i = 0u; i < state->count; i++) {
        if (state->markers[i].id == id) return (int)i;
    }
    return -1;
}

static bool StateIsValid(const MapMarkerState *state)
{
    if (!state || state->count > MAP_MARKERS_TOTAL || state->nextId == 0u) {
        return false;
    }
    for (uint32_t i = 0u; i < state->count; i++) {
        const MapMarker *marker = &state->markers[i];
        if (marker->id == 0u || !SurfaceIsValid(marker->surface) ||
            !isfinite(marker->x) || !isfinite(marker->z) ||
            marker->color < 0 || marker->color >= MAP_MARKER_COLOR_COUNT ||
            !MapMarkerNameIsValid(marker->name)) {
            return false;
        }
        int surfaceCount = 0;
        for (uint32_t j = 0u; j < state->count; j++) {
            if (i != j && marker->id == state->markers[j].id) return false;
            if (SameSurface(marker->surface, state->markers[j].surface)) {
                surfaceCount++;
            }
        }
        if (surfaceCount > MAP_MARKERS_PER_SURFACE) return false;
    }
    return state->targetId == 0u ||
           FindIndexInState(state, state->targetId) >= 0;
}

bool MapMarkersInstallState(const MapMarkerState *state)
{
    if (!StateIsValid(state)) return false;
    mapMarkers = *state;
    return true;
}

static bool WriteRecord(FILE *file, const MapMarker *marker)
{
    uint32_t dimension = (uint32_t)marker->surface.dimension;
    uint8_t color = (uint8_t)marker->color;
    uint8_t nameLength = (uint8_t)strlen(marker->name);
    return fwrite(&marker->id, sizeof(marker->id), 1, file) == 1 &&
           fwrite(&dimension, sizeof(dimension), 1, file) == 1 &&
           fwrite(&marker->surface.surfaceId,
                  sizeof(marker->surface.surfaceId), 1, file) == 1 &&
           fwrite(&marker->x, sizeof(marker->x), 1, file) == 1 &&
           fwrite(&marker->z, sizeof(marker->z), 1, file) == 1 &&
           fwrite(&color, sizeof(color), 1, file) == 1 &&
           fwrite(&nameLength, sizeof(nameLength), 1, file) == 1 &&
           fwrite(marker->name, 1, nameLength, file) == nameLength;
}

bool MapMarkersSaveState(FILE *file)
{
    uint32_t version = MAP_MARKER_STATE_VERSION;
    if (!file || !StateIsValid(&mapMarkers) ||
        fwrite(&version, sizeof(version), 1, file) != 1 ||
        fwrite(&mapMarkers.count, sizeof(mapMarkers.count), 1, file) != 1 ||
        fwrite(&mapMarkers.nextId, sizeof(mapMarkers.nextId), 1, file) != 1 ||
        fwrite(&mapMarkers.targetId, sizeof(mapMarkers.targetId), 1, file) != 1) {
        return false;
    }
    for (uint32_t i = 0u; i < mapMarkers.count; i++) {
        if (!WriteRecord(file, &mapMarkers.markers[i])) return false;
    }
    return true;
}

static bool ReadRecord(FILE *file, MapMarker *marker)
{
    uint32_t dimension = 0u;
    uint8_t color = 0u;
    uint8_t nameLength = 0u;
    memset(marker, 0, sizeof(*marker));
    if (fread(&marker->id, sizeof(marker->id), 1, file) != 1 ||
        fread(&dimension, sizeof(dimension), 1, file) != 1 ||
        fread(&marker->surface.surfaceId,
              sizeof(marker->surface.surfaceId), 1, file) != 1 ||
        fread(&marker->x, sizeof(marker->x), 1, file) != 1 ||
        fread(&marker->z, sizeof(marker->z), 1, file) != 1 ||
        fread(&color, sizeof(color), 1, file) != 1 ||
        fread(&nameLength, sizeof(nameLength), 1, file) != 1 ||
        nameLength == 0u || nameLength >= MAP_MARKER_NAME_SIZE ||
        fread(marker->name, 1, nameLength, file) != nameLength) {
        return false;
    }
    marker->surface.dimension = (WorldDimension)dimension;
    marker->color = (MapMarkerColor)color;
    marker->name[nameLength] = '\0';
    return true;
}

bool MapMarkersReadState(FILE *file, MapMarkerState *out)
{
    if (!file || !out) return false;
    MapMarkerState loaded;
    MapMarkersEmptyState(&loaded);
    uint32_t version = 0u;
    if (fread(&version, sizeof(version), 1, file) != 1 ||
        version != MAP_MARKER_STATE_VERSION ||
        fread(&loaded.count, sizeof(loaded.count), 1, file) != 1 ||
        fread(&loaded.nextId, sizeof(loaded.nextId), 1, file) != 1 ||
        fread(&loaded.targetId, sizeof(loaded.targetId), 1, file) != 1 ||
        loaded.count > MAP_MARKERS_TOTAL) {
        return false;
    }
    for (uint32_t i = 0u; i < loaded.count; i++) {
        if (!ReadRecord(file, &loaded.markers[i])) return false;
    }
    if (!StateIsValid(&loaded)) return false;
    *out = loaded;
    return true;
}

int MapMarkersCount(MapMarkerSurface surface)
{
    int count = 0;
    for (uint32_t i = 0u; i < mapMarkers.count; i++) {
        if (SameSurface(surface, mapMarkers.markers[i].surface)) count++;
    }
    return count;
}

int MapMarkersCollect(MapMarkerSurface surface, MapMarker *out, int capacity)
{
    if (!out || capacity <= 0) return 0;
    int count = 0;
    for (uint32_t i = 0u; i < mapMarkers.count && count < capacity; i++) {
        if (!SameSurface(surface, mapMarkers.markers[i].surface)) continue;
        out[count++] = mapMarkers.markers[i];
    }
    return count;
}

bool MapMarkersFind(uint32_t id, MapMarker *out)
{
    int index = FindIndexInState(&mapMarkers, id);
    if (index < 0) return false;
    if (out) *out = mapMarkers.markers[index];
    return true;
}

static uint32_t AllocateId(void)
{
    uint32_t candidate = mapMarkers.nextId;
    do {
        if (candidate == 0u) candidate = 1u;
        if (FindIndexInState(&mapMarkers, candidate) < 0) {
            mapMarkers.nextId = candidate + 1u;
            if (mapMarkers.nextId == 0u) mapMarkers.nextId = 1u;
            return candidate;
        }
        candidate++;
    } while (candidate != mapMarkers.nextId);
    return 0u;
}

bool MapMarkersCreate(MapMarkerSurface surface, float x, float z,
                      const char *name, MapMarkerColor color,
                      uint32_t *outId)
{
    if (!SurfaceIsValid(surface) || !isfinite(x) || !isfinite(z) ||
        !MapMarkerNameIsValid(name) || color < 0 ||
        color >= MAP_MARKER_COLOR_COUNT ||
        mapMarkers.count >= MAP_MARKERS_TOTAL ||
        MapMarkersCount(surface) >= MAP_MARKERS_PER_SURFACE) {
        return false;
    }
    uint32_t id = AllocateId();
    if (id == 0u) return false;
    MapMarker *marker = &mapMarkers.markers[mapMarkers.count++];
    *marker = (MapMarker){
        .id = id, .surface = surface, .x = x, .z = z, .color = color
    };
    snprintf(marker->name, sizeof(marker->name), "%s", name);
    if (outId) *outId = id;
    return true;
}

bool MapMarkersUpdate(uint32_t id, const char *name, MapMarkerColor color)
{
    int index = FindIndexInState(&mapMarkers, id);
    if (index < 0 || !MapMarkerNameIsValid(name) || color < 0 ||
        color >= MAP_MARKER_COLOR_COUNT) {
        return false;
    }
    mapMarkers.markers[index].color = color;
    snprintf(mapMarkers.markers[index].name,
             sizeof(mapMarkers.markers[index].name), "%s", name);
    return true;
}

bool MapMarkersRemove(uint32_t id)
{
    int index = FindIndexInState(&mapMarkers, id);
    if (index < 0) return false;
    if (mapMarkers.targetId == id) mapMarkers.targetId = 0u;
    mapMarkers.count--;
    if ((uint32_t)index < mapMarkers.count) {
        memmove(&mapMarkers.markers[index], &mapMarkers.markers[index + 1],
                (mapMarkers.count - (uint32_t)index) * sizeof(MapMarker));
    }
    memset(&mapMarkers.markers[mapMarkers.count], 0, sizeof(MapMarker));
    return true;
}

bool MapMarkersToggleTarget(uint32_t id)
{
    if (FindIndexInState(&mapMarkers, id) < 0) return false;
    return MapMarkersSetTarget(mapMarkers.targetId == id ? 0u : id);
}

bool MapMarkersSetTarget(uint32_t id)
{
    if (id != 0u && FindIndexInState(&mapMarkers, id) < 0) return false;
    mapMarkers.targetId = id;
    return true;
}

bool MapMarkersTarget(MapMarker *out)
{
    return mapMarkers.targetId != 0u &&
           MapMarkersFind(mapMarkers.targetId, out);
}

bool MapMarkersTargetOnSurface(MapMarkerSurface surface, MapMarker *out)
{
    MapMarker target;
    if (!MapMarkersTarget(&target) || !SameSurface(surface, target.surface)) {
        return false;
    }
    if (out) *out = target;
    return true;
}

uint32_t MapMarkersTargetId(void)
{
    return mapMarkers.targetId;
}

bool MapMarkerGreatCircle(float fromLongitude, float fromLatitude,
                          float toLongitude, float toLatitude,
                          float *outBearing, float *outDistance)
{
    if (!isfinite(fromLongitude) || !isfinite(fromLatitude) ||
        !isfinite(toLongitude) || !isfinite(toLatitude) ||
        !outBearing || !outDistance) {
        return false;
    }
    float deltaLongitude = atan2f(sinf(toLongitude - fromLongitude),
                                  cosf(toLongitude - fromLongitude));
    float deltaLatitude = toLatitude - fromLatitude;
    float sinHalfLatitude = sinf(deltaLatitude * 0.5f);
    float sinHalfLongitude = sinf(deltaLongitude * 0.5f);
    float haversine = sinHalfLatitude * sinHalfLatitude +
        cosf(fromLatitude) * cosf(toLatitude) *
        sinHalfLongitude * sinHalfLongitude;
    haversine = fminf(fmaxf(haversine, 0.0f), 1.0f);
    float centralAngle = 2.0f * atan2f(sqrtf(haversine),
                                       sqrtf(fmaxf(0.0f, 1.0f - haversine)));
    float y = sinf(deltaLongitude) * cosf(toLatitude);
    float x = cosf(fromLatitude) * sinf(toLatitude) -
        sinf(fromLatitude) * cosf(toLatitude) * cosf(deltaLongitude);
    *outBearing = centralAngle < 0.000001f ? 0.0f : atan2f(y, x);
    *outDistance = centralAngle * SURFACE_RADIUS_BLOCKS;
    return isfinite(*outBearing) && isfinite(*outDistance);
}
