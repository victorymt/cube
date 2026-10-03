#include "app/game_debug_landform.h"
#include "app/game_internal.h"
#include "world/landform_runtime.h"
#include "world/terrain_generation.h"
#include "world/world.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool Available(void)
{
    return WorldCurrentSurfaceContext().kind == WORLD_SURFACE_HOME &&
        !WorldNetherIsActive() && LandformRuntimeAtlas() != NULL;
}

static SurfaceMapCell Observer(const GameRuntime *game)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    return SurfaceCanonicalMapCell(game->player.position.x + context.originX,
                                   game->player.position.z + context.originZ);
}

static void ReplySample(GameRuntime *game, SurfaceMapCell cell)
{
    SurfaceTerrainSample terrain = SurfaceTerrainAt(cell.x, cell.z, WorldTerrainMode());
    const LandformSample *s = &terrain.landform;
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL landform kind=%s x=%d z=%d elevation=%.3f water=%d water_level=%.3f "
        "watershed=%u reach=%u area=%.3f slope=%.6f wetness=%.3f gravel=%.3f sand=%.3f "
        "fines=%.3f sediment=%.3f version=%u\n",
        LandformKindName(s->kind), cell.x, cell.z, s->elevation, s->hasWater, s->waterLevel,
        s->watershed, s->reach, s->area, s->slope, s->wetness, s->gravel, s->sand,
        s->fines, s->sedimentDepth, TerrainGenerationVersion());
}

// Sample a fixed set of atlas vertices, reach centers and banks. Bounded work,
// deterministic order, no mutation of terrain, RNG, player or streaming state.
static void Find(GameRuntime *game, SurfaceMapCell observer)
{
    DebugControl *control = &game->debugControl;
    const LandformAtlas *atlas = LandformRuntimeAtlas();
    Vector3 origin = SurfaceDirectionFromMapCoordinates(observer.x + .5f, observer.z + .5f);
    float best = (float)control->landformRadius + .001f;
    control->landformFound = false;
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        const LandformCell *c = &atlas->cells[i];
        const LandformCell *d = c->downstream == LANDFORM_NONE ? c : &atlas->cells[c->downstream];
        float margin = Vector3Distance(c->direction, d->direction) * SURFACE_RADIUS_BLOCKS + 64;
        if (Vector3Distance(origin, c->direction) * SURFACE_RADIUS_BLOCKS > best + margin) continue;
        Vector3 side = Vector3Normalize(Vector3CrossProduct(c->direction, d->direction));
        for (int k = 0; k < 9; k++) {
            float t = (k % 3) * .5f;
            float bank = k / 3 == 0 ? 0 : (k / 3 == 1 ? 2.0f : -2.0f);
            Vector3 p = Vector3Normalize(Vector3Add(Vector3Lerp(c->direction, d->direction, t),
                Vector3Scale(side, bank * c->width / SURFACE_RADIUS_BLOCKS)));
            SurfaceMapCell cell;
            if (!SurfaceAddressCanonicalMapCell(SurfaceAddressFromDirection(0, p, 0), &cell)) continue;
            Vector3 actual = SurfaceDirectionFromMapCoordinates(cell.x + .5f, cell.z + .5f);
            float distance = 2 * SURFACE_RADIUS_BLOCKS * asinf(fminf(1, Vector3Distance(origin, actual) * .5f));
            if (distance >= best || distance > control->landformRadius) continue;
            SurfaceTerrainSample terrain = SurfaceTerrainAt(cell.x, cell.z, WorldTerrainMode());
            if (strcmp(LandformKindName(terrain.landform.kind), control->landformType)) continue;
            best = distance;
            control->landformFound = true;
            control->landformFoundX = cell.x;
            control->landformFoundZ = cell.z;
            control->landformFoundY = (int)ceilf(fmaxf(terrain.elevation,
                terrain.landform.hasWater ? terrain.landform.waterLevel : terrain.elevation)) + 3;
        }
    }
    DebugControlReply(control, "DEBUG_CONTROL landform find found=%d type=%s radius=%u\n",
        control->landformFound, control->landformType, control->landformRadius);
    if (control->landformFound)
        ReplySample(game, (SurfaceMapCell){ control->landformFoundX, control->landformFoundZ });
}

bool GameDebugLandformDispatch(GameRuntime *game, DebugControlCommand command)
{
    if (command != DEBUG_CONTROL_COMMAND_LANDFORM_INSPECT &&
        command != DEBUG_CONTROL_COMMAND_LANDFORM_FIND) return false;
    game->debugControl.landformFound = false;
    if (!Available()) {
        game->debugCommandFailed = true;
        snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "landform_unavailable");
        DebugControlReply(&game->debugControl, "DEBUG_CONTROL landform unavailable\n");
    } else if (command == DEBUG_CONTROL_COMMAND_LANDFORM_INSPECT) ReplySample(game, Observer(game));
    else Find(game, Observer(game));
    return true;
}

bool GameDebugLandformResolve(GameRuntime *game, const char *name, DebugDslValue *out)
{
    if (strncmp(name, "landform.", 9)) return false;
    const char *field = name + 9;
    bool available = Available();
    if (!strcmp(field, "available") || !strcmp(field, "found")) {
        out->type = DEBUG_DSL_VALUE_BOOL;
        out->as.boolean = available && (!strcmp(field, "available") || game->debugControl.landformFound);
        return true;
    }
    if (!strcmp(field, "position")) {
        WorldSurfaceContext context = WorldCurrentSurfaceContext();
        out->type = DEBUG_DSL_VALUE_VEC3;
        out->as.vec3 = (DebugDslVec3){ game->debugControl.landformFoundX - context.originX,
            game->debugControl.landformFoundY, game->debugControl.landformFoundZ - context.originZ };
        return true;
    }
    if (!available) return false;
    SurfaceMapCell cell = Observer(game);
    LandformSample s = SurfaceTerrainAt(cell.x, cell.z, WorldTerrainMode()).landform;
    if (!strcmp(field, "kind")) {
        out->type = DEBUG_DSL_VALUE_STRING;
        out->as.string = LandformKindName(s.kind);
        return true;
    }
    if (!strcmp(field, "has_water")) {
        out->type = DEBUG_DSL_VALUE_BOOL;
        out->as.boolean = s.hasWater;
        return true;
    }
    out->type = DEBUG_DSL_VALUE_NUMBER;
    if (!strcmp(field, "elevation")) out->as.number = s.elevation;
    else if (!strcmp(field, "water_level")) out->as.number = s.waterLevel;
    else if (!strcmp(field, "area")) out->as.number = s.area;
    else if (!strcmp(field, "slope")) out->as.number = s.slope;
    else if (!strcmp(field, "wetness")) out->as.number = s.wetness;
    else if (!strcmp(field, "sediment")) out->as.number = s.sedimentDepth;
    else if (!strcmp(field, "version")) out->as.number = TerrainGenerationVersion();
    else return false;
    return true;
}
