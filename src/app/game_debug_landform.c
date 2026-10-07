#include "app/game_debug_landform.h"
#include "app/game_internal.h"
#include "world/landform_runtime.h"
#include "world/terrain_generation.h"
#include "world/world.h"
#include "world/world_surface_snapshot.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool Available(void)
{
    WorldSurfaceKind kind = WorldCurrentSurfaceContext().kind;
    return (kind == WORLD_SURFACE_HOME || kind == WORLD_SURFACE_PLANET) &&
        !WorldNetherIsActive() && LandformRuntimeAtlas() != NULL;
}

static const char *SurfaceName(WorldSurfaceKind kind)
{
    switch (kind) {
    case WORLD_SURFACE_HOME: return "home";
    case WORLD_SURFACE_PLANET: return "planet";
    default: return "none";
    }
}

static SurfaceMapCell Observer(const GameRuntime *game)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    return SurfaceCanonicalMapCell(game->player.position.x + context.originX,
                                   game->player.position.z + context.originZ);
}

static SurfaceTerrainSample SampleAt(SurfaceMapCell cell)
{
    if (WorldCurrentSurfaceContext().kind != WORLD_SURFACE_PLANET) {
        return SurfaceTerrainAt(cell.x, cell.z, WorldTerrainMode());
    }
    return PlanetTerrainSampleAt(cell.x, cell.z);
}

static void ReplySample(GameRuntime *game, SurfaceMapCell cell)
{
    SurfaceTerrainSample terrain = SampleAt(cell);
    const LandformSample *s = &terrain.landform;
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    const PlanetLandformStrategy *strategy = LandformRuntimeStrategy();
    LandformPhysicsDiagnostic physics = { 0 };
    LandformRuntimePhysicsDiagnosticAt(cell.x, cell.z, &physics);
    Vector3 direction = LandformRuntimeDirectionAt(cell.x, cell.z);
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL landform kind=%s x=%d z=%d elevation=%.3f water=%d water_level=%.3f "
        "watershed=%u reach=%u area=%.3f slope=%.6f wetness=%.3f gravel=%.3f sand=%.3f "
        "fines=%.3f sediment=%.3f version=%u strategy=%s surface=%s body=%u "
        "capabilities=%u latitude=%.6f angular_velocity=%.9g coriolis=%.9g "
        "deflection_blocks=%.6g\n",
        LandformKindName(s->kind), cell.x, cell.z, s->elevation, s->hasWater, s->waterLevel,
        s->watershed, s->reach, s->area, s->slope, s->wetness, s->gravel, s->sand,
        s->fines, s->sedimentDepth, TerrainGenerationVersion(),
        strategy ? LandformStrategyName(strategy->id) : "none",
        SurfaceName(context.kind), context.bodyId,
        LandformRuntimeCapabilities(), asin(Clamp(direction.y, -1.0f, 1.0f)),
        physics.angularVelocity, physics.coriolisParameter,
        physics.deflectionBlocks);
}

// Sample a fixed set of atlas vertices, reach centers and banks. Bounded work,
// deterministic order, no mutation of terrain, RNG, player or streaming state.
static void Find(GameRuntime *game, SurfaceMapCell observer)
{
    DebugControl *control = &game->debugControl;
    const LandformAtlas *atlas = LandformRuntimeAtlas();
    Vector3 origin = LandformRuntimeDirectionAt(observer.x, observer.z);
    float best = (float)control->landformRadius + .001f;
    control->landformFound = false;
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        const LandformCell *c = &atlas->cells[i];
        const LandformCell *d = c->downstream == LANDFORM_NONE ? c : &atlas->cells[c->downstream];
        float margin = Vector3Distance(c->direction, d->direction) * SURFACE_RADIUS_BLOCKS + 64;
        if (Vector3Distance(origin, c->direction) * SURFACE_RADIUS_BLOCKS > best + margin) continue;
        Vector3 side = Vector3Normalize(Vector3CrossProduct(c->direction, d->direction));
        int candidates = !strcmp(control->landformType, "terrace") ? 15 : 9;
        for (int k = 0; k < candidates; k++) {
            float t = (k % 3) * .5f;
            float bank = k / 3 == 0 ? 0 : (k / 3 == 1 ? 2.0f : -2.0f);
            if (k >= 9) bank = k / 3 == 3 ? 5.0f : -5.0f;
            Vector3 p = Vector3Normalize(Vector3Add(Vector3Lerp(c->direction, d->direction, t),
                Vector3Scale(side, bank * c->width / SURFACE_RADIUS_BLOCKS)));
            SurfaceMapCell cell;
            if (!SurfaceAddressCanonicalMapCell(SurfaceAddressFromDirection(0, p, 0), &cell)) continue;
            Vector3 actual = LandformRuntimeDirectionAt(cell.x, cell.z);
            float distance = 2 * SURFACE_RADIUS_BLOCKS * asinf(fminf(1, Vector3Distance(origin, actual) * .5f));
            if (distance >= best || distance > control->landformRadius) continue;
            SurfaceTerrainSample terrain = SampleAt(cell);
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
        // Return column centers so spherical rebase rounding cannot select
        // the neighboring column after teleporting to an integer boundary.
        out->as.vec3 = (DebugDslVec3){ game->debugControl.landformFoundX + .5 - context.originX,
            game->debugControl.landformFoundY, game->debugControl.landformFoundZ + .5 - context.originZ };
        return true;
    }
    if (!available) return false;
    SurfaceMapCell cell = Observer(game);
    const PlanetLandformStrategy *strategy = LandformRuntimeStrategy();
    if (!strcmp(field, "strategy") || !strcmp(field, "surface")) {
        out->type = DEBUG_DSL_VALUE_STRING;
        out->as.string = !strcmp(field, "strategy")
            ? LandformStrategyName(strategy->id)
            : SurfaceName(WorldCurrentSurfaceContext().kind);
        return true;
    }
    if (!strcmp(field, "coriolis_enabled")) {
        out->type = DEBUG_DSL_VALUE_BOOL;
        out->as.boolean = false; // Diagnostic only in all currently supported versions.
        return true;
    }
    if (!strcmp(field, "latitude") || !strcmp(field, "angular_velocity") ||
        !strcmp(field, "coriolis") || !strcmp(field, "deflection_blocks") ||
        !strcmp(field, "resolvable_deflection")) {
        LandformPhysicsDiagnostic physics = { 0 };
        LandformRuntimePhysicsDiagnosticAt(cell.x, cell.z, &physics);
        if (!strcmp(field, "resolvable_deflection")) {
            out->type = DEBUG_DSL_VALUE_BOOL;
            out->as.boolean = physics.resolvableDeflection;
        } else {
            out->type = DEBUG_DSL_VALUE_NUMBER;
            out->as.number = !strcmp(field, "latitude") ? physics.latitude :
                !strcmp(field, "angular_velocity") ? physics.angularVelocity :
                !strcmp(field, "coriolis") ? physics.coriolisParameter : physics.deflectionBlocks;
        }
        return true;
    }
    LandformSample s = SampleAt(cell).landform;
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
    else if (!strcmp(field, "body")) out->as.number = WorldCurrentSurfaceContext().bodyId;
    else if (!strcmp(field, "capabilities")) out->as.number = strategy->capabilities;
    else return false;
    return true;
}
